# SpDiag SuperPoD 性能诊断工具

SpDiag（SuperPoD Diagnostics）是一套面向超节点应用的性能诊断工具。该工具可实现代码段运行时长的记录与统计，并结合 eBPF 与 perf_event 实现内存占用率监测与缓存命中率观测。

SpDiag 已应用于Mooncake、brpc等项目在UB使能过程中的性能诊断。今后将基于URMA、UB memory的能力实现分布式集群中的多机联合性能诊断，并诊断UB组件本身的性能问题。

## SpDiag 能解决什么问题

| 诊断问题 | 使用功能 | 是否改造目标程序 | 主要输出 |
| --- | --- | --- | --- |
| 哪个业务阶段慢？调用次数和长尾情况怎样？ | **PerfPoint** | 需要集成 SDK | 点位调用次数、成功/失败次数、总耗时、平均/最小/最大耗时和可选分位数 |
| 进程内存由哪些调用栈分配？是否存在未释放增长？ | **Memstat** | 不需要 | 调用栈、当前/峰值占用、累计分配量和分配/释放次数 |
| 内存属于哪类业务对象、容器或字段？ | **MemPoint** | 需要集成 SDK | 按业务点位归因的内存生命周期、当前/峰值占用和对象字段构成 |
| CPU 是否受到缓存、TLB 或分支行为影响？ | **Cachestat** | 不需要 | 各类 PMU 事件的访问次数、miss、命中率、MPKI 等指标 |

简单来说：**PerfPoint 定位时间消耗问题，Memstat 与 MemPoint 定位内存泄露与异常分配，Cachestat 分析硬件执行效率。**

## 核心功能

| 功能 | 典型入口 | 目标程序改造 | 运行权限 | 主要构建开关 |
| --- | --- | --- | --- | --- |
| PerfPoint | `spdiag show/watch/history` | 集成 C++ SDK | 普通用户 | 默认提供 |
| Memstat | `spdiag memstat --pid <PID>` | 无 | 当前建议 root；插件声明 `CAP_SYS_ADMIN` + `CAP_BPF` | `-o on`，`build.sh` 默认开启 |
| MemPoint | `spdiag mempoint --pid <PID>` | 集成 C++ SDK 和 USDT 点位 | 当前建议 root；插件声明 `CAP_SYS_ADMIN` + `CAP_BPF` | `-m on`，`build.sh` 默认关闭 |
| Cachestat | `spdiag cachestat --pid <PID>` | 无 | 当前建议 root；插件声明 `CAP_PERFMON` | `-k on`，`build.sh` 默认开启 |

PerfLog 使用 `-s on` 构建，分位数使用 `-p on` 构建。详细依赖和全部开关见 [安装指南](INSTALLATION.md#编译选项buildsh)。

### 1. PerfPoint：定位代码路径耗时

当请求延迟升高，但还不知道时间花在计算、IO、锁等待还是某个处理阶段时，可以在关键路径加入命名 PerfPoint。SpDiag 会按点位汇总调用次数、成功/失败次数、总耗时以及平均、最小和最大耗时；使用默认关闭的 `-p on` 构建后，还会计算 P99/P999/P9999。

PerfPoint 使用编译期定义的点位和共享内存分核写入，热路径不执行字符串查找、哈希查找或互斥锁操作。适合在高频路径中保留长期观测点。

#### 安装 SDK 并链接业务程序

通过 yum 仓库安装动态 SDK（推荐）：

```bash
sudo yum install spdiag-devel
```

如果业务需要静态链接，安装 `spdiag-static`，并在 CMake 中改用 `SpDiag::spdiag_static`。也可以执行 `bash build.sh -r -l on` 从源码安装动态 SDK。业务项目的 CMake 配置如下：

```cmake
find_package(SpDiag CONFIG REQUIRED)
target_include_directories(my_service PRIVATE ${CMAKE_CURRENT_SOURCE_DIR}/include) # 定位业务 .def 文件
target_link_libraries(my_service PRIVATE SpDiag::spdiag_lib)
```

`SpDiag::spdiag_lib` 会自动传递 SDK 头文件目录、共享库及所需系统依赖，不需要手工添加 `-I`、`-L`、`-lspdiag`、`-lpthread` 或 `-lrt`。

#### 定义业务点位

建议在业务仓库内维护点位定义，不要修改 SpDiag 源码。先创建 `include/my_perf_points.def`：

```cpp
PERF_KEY_DEF(REQUEST_PARSE,   "Request", "Parse")
PERF_KEY_DEF(REQUEST_EXECUTE, "Request", "Execute")
PERF_KEY_DEF(STORAGE_WRITE,   "Storage", "Write")
```

三列依次为业务代码使用的枚举名、CLI 展示的模块名和点位名。再创建统一包装头 `include/my_perf_points.h`：

```cpp
#pragma once

#define SPDIAG_PERF_DEF_FILE "my_perf_points.def" // 指定.def文件
#define SPDIAG_PROGRAM_NAME "my_service" // 自定义程序名称，不指定默认使用PID
#include <spdiag/auto_perf.h>
```

所有使用点位的源文件都应包含同一个包装头，保证点位顺序一致。修改 `.def` 后只需重新编译业务程序，不需要重新编译 SpDiag。

#### 在关键路径中打点

```cpp
#include "my_perf_points.h"

int HandleRequest() {
    SpDiag::PerfPoint point(PerfKey::REQUEST_EXECUTE, SpDiag::PerfLevel::KEY_MODULE);
    point.Start();
    int rc = ExecuteRequest();
    point.End(rc);
    return rc;
}
```

`End(0)` 计入成功，非零返回码计入失败。必须显式调用 `End()`；未结束的对象析构时记为 Abandon，不会自动记为成功。存在提前返回时，也应在返回前调用 `End(errorCode)`。

#### 启动采集并查看结果

```bash
spdiag start                         # 创建 PerfPoint 共享内存
./build/my_service                   # 启动已集成 SDK 的业务程序
spdiag show                          # 查看当前汇总
spdiag show --detail                 # 查看详细统计
spdiag watch --interval 1000         # 每秒持续刷新
spdiag stop                          # 结束采集并销毁共享内存
```

业务程序未执行 `spdiag start` 时仍能正常运行，只是不产生可展示的数据。默认共享内存支持业务先启动、之后执行 `spdiag start` 自动重连。

当汇总统计不足以解释单次抖动时，可以启用 **PerfLog**，查看探针最近调用的时间、线程和单次耗时。异步流程还可以使用 **Global PerfPoint** 在不同线程中执行 `Start` 和 `End`。完整接入方式见 [QuickStart](QuickStart.md#功能一perfpoint-打点计时)，性能测试方法见 [Benchmark 指南](examples/BENCHMARK_README.md)。

#### 预期输出

`spdiag show` 的汇总表如下。下面启用了 `-p on`，因此末尾包含三个分位数列；未启用时这些列不会出现。`Not` 是调用了 `Start()` 但未调用 `End()`、最终按 Abandon 处理的次数。点位和数值取决于业务负载，表格格式与列名来自当前显示实现。

```text
 #     Program    Module     Point  Lvl     Ticks      Good       Bad       Not       Total(ns)       Avg(ns)       Min(ns)       Max(ns)       P99(ns)      P999(ns)     P9999(ns)
 ----  ---------  ---------  -----  ---  --------  --------  --------  --------  --------------  ------------  ------------  ------------  ------------  ------------  ------------
    1  my_service Request    Execute    2      1000       990        10         0      1000000000       1000000        800000       2000000       1800000       1950000       2000000
    2  my_service Storage    Write      2       200       200         0         0       500000000       2500000       2100000       3100000       2900000       3050000       3100000
```

使用 `-s on` 构建并以 `spdiag start --perflog` 启动后，`spdiag show --perflog` 改为输出单次探针记录；每个点位最多保留最近 100 条：

```text
 Datetime                            PID       TID    CPU  Level  Program         Module.Point                  Cost(ns)
 ----------------------------------  ------  --------  -----  -----  --------------  ----------------------------  ----------
 2026-06-10 14:32:01.123456789       12345     12346      3      2  my_service      Request.Execute                 1500
```

### 2. Memstat：从调用栈定位进程内存

当进程 RSS 持续上涨、分配频率异常或怀疑存在未释放内存时，Memstat 可以直接附加到运行中的进程。它通过 eBPF uprobe 跟踪 libc 的 `malloc`、`calloc`、`realloc` 和 `free`，并将分配行为归并到线程和调用栈。

输出包括当前占用、累计分配量、峰值、分配/释放次数以及分配调用栈，可以回答“哪条调用路径分配了内存、目前保留了多少”。目标程序无需集成 SpDiag SDK，但为了获得完整符号和源码位置，建议保留调试信息和栈回溯信息。

```bash
# 终端 1：启动仓库中的泄漏负载并等待采集器附加
./build/examples/memstat_demo --mode leak --count 200 --wait

# 终端 2：附加后回到终端 1 按 Enter 运行负载
sudo spdiag memstat --pid $(pidof memstat_demo)
```

Memstat 适合定位分配热点、内存周转和疑似泄漏路径；这里的泄漏指标来自分配与释放记录的差异，是诊断线索，不代替离线内存正确性检查。完整参数和编译建议见 [CLI 参考](docs/zh/CLI_REFERENCE.md#memstat)。

#### 预期输出

默认输出按 `TID + CallStack` 聚合，并按当前占用、峰值和分配次数排序。以下是运行 `memstat_demo --mode leak --count 200 --wait` 时的典型外观；PID、TID、时间和额外的 libc 分配记录会随环境变化。

```text
    #    PID  TID      CallStack          Current      Total      Allocs       Frees       Peak    AvgSize    MaxSize  FirstSeen
 ----  -----  -------  ----------------  ---------  ---------  ----------  ----------  ---------  ---------  ---------  ---------
    1  54321  54321    WorkloadLeak       200 KiB    400 KiB         200         100    200 KiB   2.00 KiB   2.00 KiB   14:30:01

* Peak in aggregated views is max(per-record peak); may underestimate true peak.
[aggregator] groups=1 (from 1 raw records), sum=200 KiB, allocs=200, frees=100
```

### 3. MemPoint：把内存归因到业务对象

仅凭 `malloc` 调用栈，有时无法区分同一分配器创建的 SQL Plan、RPC Buffer、缓存节点等不同业务对象。MemPoint 允许开发者在 SDK 中定义业务内存点位，并通过 USDT 探针标记对象的进入、离开和大小变化。

MemPoint 支持 RAII 跟踪、受跟踪容器和对象字段统计。CLI 按业务分类和 Key 展示当前占用、累计分配、峰值、分配/释放次数等数据，从而把底层分配行为转换为业务语义。

```cpp
#include <spdiag/mem_point.h>

void HandleBuffer(void* buffer, uint64_t bufferSize) {
    auto tracked = SpDiag::MemPoint::TrackBuffer(
        SpDiag::MemPointKey::RPC_REQUEST_BUF, buffer, bufferSize);

    // tracked 析构时结束该对象的生命周期记录
}
```

```bash
# 终端 1：运行 MemPoint demo
./build/examples/mempoint_demo --wait

# 终端 2：附加后回到终端 1 按 Enter 运行负载
sudo spdiag mempoint --pid $(pidof mempoint_demo)
```

MemPoint 回答“这块内存属于什么业务对象”的问题。MemPoint 需要目标程序接入 SDK，并在构建 SpDiag 时启用 `-m on`、安装提供 `sys/sdt.h` 的依赖。完整接入方式见 [QuickStart](QuickStart.md#功能五mempoint-点位级内存观测)。

#### 预期输出

```text
    #  Category    Key                       Current      Total      Allocs       Frees       Peak    AvgSize    MaxSize  FirstSeen
 ----  ----------  ----------------------  ---------  ---------  ----------  ----------  ---------  ---------  ---------  ---------
    1  Transacti…  Leak                      400 KiB    400 KiB         100           0    400 KiB   4.00 KiB   4.00 KiB   14:30:01
    2  Rpc         Spike                         0 B   10.0 MiB           1           1   10.0 MiB   10.0 MiB   10.0 MiB   14:30:02
```

### 4. Cachestat：分析缓存与 CPU 执行效率

当 PerfPoint 已确认某段计算耗时较高，但没有明显的 IO、锁或分配问题时，可以进一步检查数据访问和硬件执行效率。Cachestat 通过 `perf_event_open` 为目标进程的线程采集 PMU 事件，无需修改目标程序。

当前实现支持 L1 数据缓存 (L1-dcache)、L1 指令缓存 (L1-icache)、末级缓存 (LLC)、数据TLB (dTLB)、指令TLB (iTLB) 和分支事件 (branch)，默认采集前三项，其他事件通过 `--cache-levels` 选择。输出包括访问次数、miss、命中率、MPKI、访问密度，以及可用时的 IPC 等 CPU 指标。它适合发现随机访问、工作集过大、数据布局不友好或分支行为异常等线索。

```bash
# 终端 1：用跨列矩阵访问制造较高 miss 负载
./build/examples/cachestat_demo --mode col-major --n 3072 --repeat 3 --wait

# 终端 2：启用全部六类事件和增量视图，再回到终端 1 按 Enter
sudo spdiag cachestat --pid $(pidof cachestat_demo) --delta \
  --cache-levels L1-dcache,L1-icache,LLC,dTLB,iTLB,branch
```

实际可用事件取决于处理器 PMU 和内核配置。完整说明见 [CLI 参考](docs/zh/CLI_REFERENCE.md#cachestat)。

#### 预期输出

默认模式累计本次监测会话的数据，`--delta` 则显示当前采集周期的增量。下面按上述命令展示全部六类事件和当前显示层的完整列。(下表数据仅作演示用)

```text
    #  Level               References              Misses   MissRate(%)    HitRate(%)        MPKI    LoadDens
 ----  ----------  ------------------  ------------------  ------------  ------------  ----------  ----------
    1  L1-dcache           1,234,567              45,678          3.70         96.30        37.0       1.000
    2  L1-icache             567,890              12,345          2.17         97.83        21.7       0.460
    3  LLC                 1,234,567             234,567         19.00         81.00       190.0       1.000
    4  dTLB                1,234,567               2,469          0.20         99.80         2.0       1.000
    5  iTLB                  567,890                 617          0.11         99.89         1.1       0.460
    6  branch                246,913               3,704          1.50         98.50        15.0       0.200
Counters: 14/6 (events/PMU) -- MULTIPLEXED

                   IPC   Eff.Freq(GHz)        GIPS
 ----------  --------  --------------  ----------
 Delta           1.85            2.40        4.44
 Avg             1.72            2.38        4.09
```

CPU 表中的 `Delta` 是当前周期值，`Avg` 是整个监测会话的平均值。

## RPM 包说明与按需安装

SpDiag 发布一次会生成三个二进制 RPM 和一个源码 RPM：

| RPM 包 | 内容与作用 | 适用场景 |
| --- | --- | --- |
| `spdiag` | `spdiag` CLI、版本化动态运行库 `libspdiag.so.*` 和 `/etc/spdiag/spdiag.conf` | 使用 PerfPoint 的采集展示命令，或直接使用 Memstat、Cachestat、MemPoint 等 CLI 功能；运行动态链接 SpDiag 的业务程序 |
| `spdiag-devel` | SDK 头文件、`libspdiag.so` 链接名和 `SpDiagConfig.cmake` | 编译动态链接 SpDiag 的 PerfPoint/MemPoint 业务程序 |
| `spdiag-static` | `libspdiag.a`、`libspdiag_logger.a` 和静态 CMake target | 编译静态链接 SpDiag 的 PerfPoint/MemPoint 业务程序 |

包依赖关系为 `spdiag-static -> spdiag-devel -> spdiag`。如果拿到的是本地 RPM 文件，建议使用 `yum install ./xxx.rpm`，让 yum 同时检查系统依赖；安装开发包或静态包时，需要把它依赖的本地 RPM 一并传入。按功能选择如下：

| 要使用的功能 | 本地 RPM 安装命令 | 说明 |
| --- | --- | --- |
| Memstat、Cachestat，或只运行 SpDiag CLI | `sudo yum install ./spdiag-<版本>-<发行号>.aarch64.rpm` | 目标程序无需集成 SDK；按命令要求使用 root 或相应 capability |
| PerfPoint/PerfLog（编译动态链接业务程序） | `sudo yum install ./spdiag-<版本>-<发行号>.aarch64.rpm ./spdiag-devel-<版本>-<发行号>.aarch64.rpm` | 编译时链接 `SpDiag::spdiag_lib` |
| PerfPoint/PerfLog（编译静态链接业务程序） | `sudo yum install ./spdiag-<版本>-<发行号>.aarch64.rpm ./spdiag-devel-<版本>-<发行号>.aarch64.rpm ./spdiag-static-<版本>-<发行号>.aarch64.rpm` | 编译时链接 `SpDiag::spdiag_static` |
| MemPoint | 动态链接安装主包和 `-devel` 包；静态链接再安装 `-static` 包 | RPM 本身必须以 `-m on` 构建；业务程序还需接入 MemPoint SDK/USDT 点位 |

以上命令适用于当前目录中已有 RPM 文件的场景。也可以使用 `sudo rpm -ivh <RPM 文件>`，但 `rpm` 不会自动从软件源解决缺失的系统依赖，因此更推荐使用 yum 安装本地文件。

RPM 发布到已配置的 yum 仓库后，安装方式才简化为直接使用包名，不需要先下载 RPM 文件，也不需要指定版本和发行号。例如：

```bash
sudo yum makecache
sudo yum install spdiag            # 仅 CLI 和运行库
sudo yum install spdiag-devel      # 动态 SDK，自动安装 spdiag
sudo yum install spdiag-static     # 静态 SDK，自动安装 devel 和 spdiag
```

安装完成后命令的调用方式不变：PerfPoint 使用 `spdiag start/show/watch/stop`，Memstat 使用 `sudo spdiag memstat --pid <PID>`，Cachestat 使用 `sudo spdiag cachestat --pid <PID>`，MemPoint 使用 `sudo spdiag mempoint --pid <PID>`。可以通过 `rpm -ql <包名>` 查看文件，通过 `spdiag --version` 和 `spdiag --help` 验证安装。分位数、PerfLog 和 MemPoint 是否可用取决于仓库中的 RPM 构建时是否分别启用了 `-p on`、`-s on` 和 `-m on`。

## 文档导航

- [构建与安装](INSTALLATION.md)：依赖、构建开关、安装产物和容器运行说明
- [功能接入与使用教程](QuickStart.md)：PerfPoint、PerfLog、Memstat、Cachestat 和 MemPoint 的操作步骤
- [完整 CLI 参考](docs/zh/CLI_REFERENCE.md)：命令、参数和输出说明
- [总体介绍](docs/zh/OVERVIEW.md)：使用场景、组件职责和实现原理
- [框架设计](docs/zh/DESIGN.md)：共享内存、SDK、运行时和管理层设计
- [测试指南](docs/zh/TEST_GUIDE.md)：构建和执行测试
- [性能测试](examples/BENCHMARK_README.md)：基准程序、环境和测试方法
- [版本变更](CHANGELOG.md)：各版本的重要变化

## 许可证

本项目基于 [木兰宽松许可证，第 2 版（Mulan PSL v2）](LICENSE) 发布。使用与分发请遵循许可证条款。
