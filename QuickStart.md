# SpDiag QuickStart - 接入指南

本文档面向用户，介绍如何将 SpDiag 集成到项目并使用各项功能。源码安装、RPM 包拆分、动态/静态 SDK 和 submodule 接入的完整说明见 [INSTALLATION.md](INSTALLATION.md)。

## 功能概览

SpDiag 提供以下功能，覆盖不同的性能分析场景：

| 功能                         | 需要改造目标程序  | 平台            | 权限要求            | 依赖共享内存 |
| -------------------------- | --------- | ------------- | --------------- | ------ |
| **PerfPoint 打点计时** | 是（集成 SDK） | 主要验证于 Linux aarch64；其他架构有兼容路径 | 无 | 是 |
| **PerfLog 探针调用日志** | 是 | 同 PerfPoint | 无 | 是 |
| **OB Memstat 内存追踪** | 否 | Linux；当前 BPF 构建目标为 ARM64 | root 或 BPF/perf capabilities | 否 |
| **OB Cachestat 缓存监控** | 否 | Linux aarch64 PMU | root、`CAP_PERFMON` 或合适的 perf 策略 | 否 |
| **MemPoint 点位级内存观测（实验功能）** | 是（集成 SDK） | Linux；当前 BPF 构建目标为 ARM64 | 通常需 root 或 BPF/perf capabilities | 否 |

***

## 功能一：PerfPoint 打点计时

PerfPoint 是 SpDiag 的核心功能，通过编译期预定义枚举和共享内存实现低开销性能打点。热路径操作为 O(1)，不做字符串查找、哈希和互斥锁操作，但仍包含计时和统计写入成本。

### 前置条件

- CMake 3.16+
- C++17 兼容编译器（GCC 9+ / Clang 10+）

### 第1步：定义性能点

在用户项目中创建 `.def` 文件（X-Macro 格式），定义你的性能点：

```cpp
// perf_points.def
PERF_KEY_DEF(COMPUTE_MATMUL,   "Compute",  "MatMul")
PERF_KEY_DEF(COMPUTE_PRIME,    "Compute",  "Prime")
PERF_KEY_DEF(IO_IOTASK,        "IO",       "IOTask")
PERF_KEY_DEF(PROCESS_WORK,     "Process",  "Work")
```

每行三个参数：枚举名、模块名（带引号）、点位名（带引号）。`auto_perf.h` 会自动读取该文件生成 `PerfKey` 枚举和名称数组。

> **自定义文件名**：默认文件名为 `perf_points.def`。如需使用其他名称，在第2步之前添加 `#define SPDIAG_PERF_DEF_FILE "my_points.def"`。文件需在编译器的 include 搜索路径中（见第4步）。

### 第2步：集成 SDK 头文件

在用户代码中包含 `auto_perf.h`：

```cpp
#define SPDIAG_PROGRAM_NAME "my_app"   // 可选，不定义则自动分配 proc_<PID>
#include "spdiag/auto_perf.h"
```

`auto_perf.h` 会完成以下工作：

- 从 `.def` 文件生成 `PerfKey` 枚举（编译期）
- 生成模块名和点位名数组（编译期）
- 首次使用 `PerfPoint` 时自动连接共享内存（运行时惰性初始化）

> **Program Name 说明**：程序名称用于在 `spdiag show` 中区分不同程序。
>
> - 手动指定：`#define SPDIAG_PROGRAM_NAME "my_app"`
> - 自动分配：不定义该宏，系统使用 `proc_<PID>` 格式（如 `proc_12345`）
>
> **高级配置**：如需自定义更多参数（`levelGate`、`dumpIntervalMs` 等），可直接使用原始 API：
>
> ```cpp
> SpDiag::PerfConfig config;
> config.programName = "my_app";
> config.slotCount = static_cast<uint32_t>(PerfKey::COUNT);
> config.levelGate = 3;
> config.dumpIntervalMs = 2000;
> SpDiag::PerfManager::Instance().Init(config);
> ```

### 第3步：添加性能监控点

使用 RAII 方式在需要监控的函数中添加打点：

```cpp
void MyFunction() {
    SpDiag::PerfPoint pt(PerfKey::COMPUTE_MATMUL,
                         SpDiag::PerfLevel::KEY_MODULE);
    pt.Start();

    // ... 被测代码 ...

    pt.End(0);  // 传入返回码，0 表示成功
}
```

`PerfKey` 是 `auto_perf.h` 从你的 `.def` 文件生成的枚举。

必须显式调用 `End()`。如果对象在未结束时析构，SDK 将其记为 Abandon，而不是自动记录一次成功调用。

### 第4步：编译你的项目

SpDiag 安装后支持 `find_package`，用户项目通过 CMake 集成：

```cmake
cmake_minimum_required(VERSION 3.16)
project(my_app LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)

find_package(SpDiag CONFIG REQUIRED)

add_executable(my_app main.cpp)
target_include_directories(my_app PRIVATE ${CMAKE_CURRENT_SOURCE_DIR})  # 让编译器找到 perf_points.def
target_link_libraries(my_app PRIVATE SpDiag::spdiag_lib)
```

说明：

- `find_package(SpDiag CONFIG REQUIRED)` 使用安装的 CMake 包；RPM 的该 target 默认指向共享 SDK
- `target_include_directories` 需包含 `.def` 文件所在目录（默认编译器会在 include 路径中查找 `"perf_points.def"`）
- 如果 `.def` 文件在子目录中，如 `defs/perf_points.def`，需将 `defs/` 加入 include 路径

> **提示**：源码默认安装在 `/usr/local`；RPM 安装在发行版标准 include/libdir。使用导出 target 后无需硬编码路径。动态链接使用 `SpDiag::spdiag_lib`，静态链接使用 `SpDiag::spdiag_static`，详见 [安装指南](INSTALLATION.md#41-选择动态链接或静态链接)。
>
> 也可直接用命令行编译：
>
> ```bash
> g++ -o myapp main.cpp -I/usr/local/include -I. -L/usr/local/lib \
>   -lspdiag -lspdiag_logger -lpthread -lrt
> ```

> **增删改打点**：修改 `.def` 文件后只需重新编译用户程序，无需重编 SpDiag 库。名称变化的 slot 数据会自动清零，名称不变的 slot 数据保留累加。

```bash
# 1. 创建共享内存（只需执行一次）
spdiag start

# 2. 运行你的应用
./my_app

# 3. 查看性能数据
spdiag show              # 查看汇总数据
spdiag show --detail     # 查看详细信息（按核心）

# 4. 销毁共享内存
spdiag stop
```

### 使用场景

| 场景 | 操作 | 结果 |
|---|---|---|
| 需要打点 | `spdiag start` → 启动被测程序 | SHM 已存在，PerfPoint 立即打点 |
| 业务进程先启动，后开监控 | 先启动被测程序 → `spdiag start` | PerfPoint 构造时惰性探测；inotify 监听 `/dev/shm`，SHM 创建后自动重连打点 |
| 不需要打点 | 直接启动被测程序（不执行 `spdiag start`） | SDK 探测 SHM 不存在时跳过统计写入，程序正常运行 |

> SDK 在每个 PerfPoint 构造时惰性探测 SHM，并通过 Linux inotify 监听默认 `/dev/shm/spdiag_shm_default`。默认名称下业务进程可以先于 `spdiag start` 启动；使用自定义 SHM 名称时，建议先创建 SHM 再启动业务程序。

### CLI 命令速查

#### 查看数据

```bash
spdiag show                     # 查看汇总数据（聚合所有核心）
spdiag show --detail            # 查看详细信息（按核心分别显示）
spdiag show --detail -c 0-1     # 按核心过滤
```

#### 持续监控

```bash
spdiag watch                    # 持续监控模式
spdiag watch --interval 5000    # 自定义监控间隔（毫秒）
```

#### 历史记录

```bash
spdiag history                  # 查看所有历史 dump 记录
spdiag history -n 10            # 查看最近 10 条
spdiag history -i 0             # 查看最新一条的完整详情（0=最新）
```

#### 排序与过滤

`--sort` 和 `--filter` 适用于 show/watch 和 memstat（命令上下文自动选择可用键）。`--csv [dir]` 可在 show、watch、history、memstat、cachestat、mempoint 中启用 CSV 落盘。

> **开销说明**：不传 `--csv` 时不会创建 CSV writer 会话；传入后在展示阶段同步写文件，不改变数据采集、排序和过滤逻辑。

**排序** `--sort KEY[:DIR][,KEY[:DIR]]...`（逗号分隔多键，左优先级最高，`:asc`/`:desc` 可省略）

```bash
spdiag show -s ticks                       # 单键 (desc)
spdiag show -s total:desc,name:asc         # 多键：先按总耗时降序，再按名称升序
spdiag memstat --pid 1 --sort leaked:desc  # memstat 按泄漏次数降序
spdiag memstat --pid 1 --sort current,allocs  # 多键
```

PerfPoint 键：`name` `lvl` `ticks` `good` `bad` `total` `avg` `min` `max`（默认 `name:asc`）
Memstat 键：`current` `peak` `total` `allocs` `frees` `leaked` `avg_size` `max_size` `min_size` `first_seen` `frame`（默认 `current,peak,allocs`）

**过滤** `--filter PAT[,PAT]...`（逗号分隔多个模式，匹配任一即保留。`*` `?` 通配符）

```bash
spdiag show --filter "Compute*"                # PerfPoint: 匹配 Module.Point
spdiag show --filter "*Stage*,*Phase*"
spdiag show -s total:desc --filter "Memory*"
spdiag memstat --pid 1 --filter "*Cache*"      # Memstat: 匹配调用栈帧名
spdiag memstat --pid 1 --sort leaked --filter "*alloc*"
```

#### 级别过滤

```bash
spdiag show --detail -l 2       # 显示 level <= 2
spdiag show --detail -l ">2"    # 显示 level > 2
spdiag show --detail -l "=3"    # 仅显示 level = 3
```

#### 持久化配置

```bash
spdiag show --history-size 50           # 设置历史记录保留 50 条
spdiag show --data-dir /custom/path     # 自定义持久化目录
spdiag show --strict-snapshot           # 快照严格模式
```

#### 管理命令

```bash
spdiag start            # 创建共享内存
spdiag stop             # 停止并销毁共享内存
spdiag restart          # 重启共享内存
spdiag clear            # 清理共享内存性能数据
spdiag status           # 查看共享内存状态
```

### 完整 CLI 参数表

| 参数                  | 短选项    | 类型     | 默认值               | 说明                                                |
| ------------------- | ------ | ------ | ----------------- | ------------------------------------------------- |
| `--detail`          | `-d`   | flag   | off               | 按核心分别显示                                           |
| `--core`            | `-c`   | string | 全部                | 指定核心范围（如 `0-3`）                                   |
| `--level`           | `-l`   | string | 全部                | 级别过滤（支持 `>`, `<`, `>=`, `<=`, `=`）                |
| `--sort`            | `-s`   | string | name:asc          | 排序（`KEY[:DIR][,KEY[:DIR]]...`，DIR 为 `asc`/`desc`） |
| `--filter`          | <br /> | string | 全部                | 通配符过滤（`GLOB[,GLOB]...`，匹配 `Module.Point`）         |
| `--interval`        | <br /> | uint   | 2000              | watch 间隔（毫秒）                                      |
| `--history-size`    | <br /> | uint   | 30                | 历史记录保留条数                                          |
| `--data-dir`        | <br /> | string | /tmp/spdiag\_data | 持久化目录                                             |
| `--strict-snapshot` | <br /> | flag   | off               | 快照严格模式                                            |
| `--csv`             | <br /> | string | —                 | CSV 落盘目录（所有输出同时生成 CSV 文件，可用 Excel 打开）       |

### 性能级别

| 级别        | 值 | 说明        |
| --------- | - | --------- |
| SubSystem | 1 | 关键路径，必须监控 |
| KeyModule | 2 | 重要模块      |
| Module    | 3 | 普通模块      |
| Debug     | 4 | 调试信息      |

### 排序字段

| 字段      | 说明               | 默认顺序 |
| ------- | ---------------- | ---- |
| `name`  | Module.Point 字典序 | 升序   |
| `lvl`   | 级别               | 升序   |
| `ticks` | 总 tick 数         | 降序   |
| `good`  | 成功次数             | 降序   |
| `bad`   | 失败次数             | 降序   |
| `total` | 总耗时（纳秒）          | 降序   |
| `avg`   | 平均耗时             | 降序   |
| `min`   | 最小耗时             | 降序   |
| `max`   | 最大耗时             | 降序   |

启用分位数后，P99/P999/P9999 会显示在输出中，但当前 PerfPoint 排序解析器不接受它们作为 `--sort` 键。

### PerfPoint 进阶：跨线程打点（全局 PerfPoint）

默认的 `PerfPoint pt(KEY)` 是**本地点**——`Start` 和 `End` 必须在同一线程执行（内部用 per-CPU shard，跨线程会导致数据竞争）。某些场景下需要**跨线程测量**（例如异步 IO worker 线程发起请求、回调线程收到响应），此时使用**全局 PerfPoint**：

```cpp
// 用法区别：第二个参数传 global_perf tag
SpDiag::PerfPoint pt(PerfKey::MY_ASYNC_OP, SpDiag::global_perf,
                     SpDiag::PerfLevel::KEY_MODULE);
pt.Start();   // 线程 A（发起请求）
// ... 请求对象通过上下文传递给线程 B ...
pt.End(0);    // 线程 B（处理完成）
```

**原理**：
- `PerfPoint(KEY, global_perf)` 触发全局模式，从 `GlobalPerfPointFactory` 单例获取一个进程级原子累加器 `GlobalAccum`
- `startTsGlobal_` 是 `std::atomic<uint64_t>`，`Start` 时 `store(release)`，`End` 时 `load(acquire)`，建立跨线程 happens-before
- 累加用 `relaxed fetch_add` + CAS min/max，多对象/多线程同 key 并发安全
- 后台 `PerfTimer` 周期性 `exchange(0)` 偷取累加器快照，写入 SHM v3 全局区

**约束**：
- 同一 `PerfPoint` 对象一次只在途一个测量（不要并发 `Start` 同一个 `pt`）
- 全局点和本地点共享 `globalSlotIndex = baseOffset + localSlot`，`spdiag show` 中同 key 自动汇总
- 越界或未初始化时 `Acquire` 返回 `nullptr`，`PerfPoint` 降级为 no-op（绝不越界写）

***

## 功能二：PerfLog 探针调用日志

PerfLog 需先集成 PerfPoint SDK。启用仅需修改编译和启动参数：

### 对比

|  | PerfPoint 打点计时 | +PerfLog 探针调用日志 |
|---|---|---|
| 编译参数 | （默认） | 额外加 `-s on` |
| 启动参数 | `spdiag start` | `spdiag start --perflog` |
| 查看数据 | `spdiag show` | `spdiag show --perflog` |
| 应用代码 | PerfPoint::End() | 无变化 |
| 输出内容 | 计数器、min/max/avg | + 每次调用的纳秒级耗时与线程 ID |

### 操作流程

```bash
# 1. 编译（PerfPoint + PerfLog）
bash build.sh -s on

# 2. 启动共享内存
sudo spdiag start --perflog

# 3. 运行应用（与 PerfPoint 完全相同）
./build/examples/demo_app

# 4. 查看日志
spdiag show --perflog             # 一次性输出
spdiag watch --perflog            # 持续刷新（Ctrl+C 退出）
```

输出示例（`|` 分隔，文本列左对齐、数值列右对齐）：

```
Datetime                         | PID   | TID     | CPU   | Level | Program        | Module.Point                  | Cost(ns)
-------------------------------------------------------------------------------------------------------------------------
2026-06-10 14:32:01.123456789    | 12345 | 12345   | 3     | 1     | my_demo        | SDK.func_entry                | 150
2026-06-10 14:32:01.234567890    | 12345 | 12346   | 1     | 2     | my_demo        | SDK.other_point               | 2300
```

| 列 | 含义 |
|---|---|
| Datetime | 挂钟时间（纳秒精度，YYYY-MM-DD HH:MM:SS.nnnnnnnnn） |
| PID | 进程 ID |
| TID | 线程 ID |
| CPU | 执行所在的 CPU 核号 |
| Level | 性能级别：1=SubSystem, 2=KeyModule, 3=Module, 4=Debug |
| Program | 注册的程序名称 |
| Module.Point | 模块名.探针名（与 PerfPoint 的 Module/Point 一致） |
| Cost(ns) | 单次执行耗时（纳秒） |

每个探针独立保留最近 100 条记录，输出按挂钟时间升序排列。

### 如果不需要 PerfLog

即使编译时已启用，启动时不加 `--perflog` 即可跳过日志记录。下次重新 `spdiag start --perflog` 即可恢复。

```bash
sudo spdiag start                 # 仅 PerfPoint，无 PerfLog
```

> 如果编译时未启用 PerfLog，运行 `spdiag show --perflog` 会提示 `--perflog is not available`。如果编译时已启用、但 `spdiag start` 时未加 `--perflog`，该命令仍可执行，只是没有 PerfLog 调用记录可供显示。
>
> PerfLog 的输出字段（Datetime / PID / TID / CPU / Level / Program / Module.Point / Cost(ns)）含义见 [README.md](README.md) 的「特性」一节。

***

## 功能三：OB Memstat 内存分配追踪

OB Memstat 是基于 eBPF uprobe 的**无侵入式**内存分配追踪功能。你不需要对目标程序做任何改造，只需指定目标进程的 PID 即可开始追踪。

### 前置条件

- **平台**：Linux aarch64
- **内核**：5.4+（需支持 BTF，即存在 `/sys/kernel/btf/vmlinux`）
- **权限**：`sudo` 或具有 `CAP_BPF` + `CAP_PERFMON` 权限
- **依赖**：libbpf 1.0+、clang、bpftool

依赖安装（openEuler 24.03 LTS）：

```bash
sudo dnf install -y cmake gcc-c++ clang libbpf-devel bpftool libelf-devel zlib-devel
```

编译：

```bash
bash build.sh
```

> OB 功能与 SHM 打点功能是独立的，**不需要**先执行 `spdiag start`。

### 第1步：确认目标进程 PID

```bash
# 方式1：按名称查找
pidof my_program

# 方式2：按名称查找（支持模式匹配）
pgrep my_program

# 方式3：进程列表过滤
ps aux | grep my_program
```

### 第2步：运行 memstat

```bash
sudo spdiag memstat --pid <PID>
```

每隔一段时间（默认 1 秒）输出一张表格，显示每个调用栈的内存分配情况。按 **Ctrl+C** 停止采集。

**输出示例：**

```
#  PID     TID    CallStack                  Current     Total    Allocs  Frees   Peak*    AvgSize   MaxSize  FirstSeen
1  54321   54322  MatMul::Compute()          8.00 MiB    16.0 MiB  2000    1000    8.00 MiB  8.19 KiB 4.00 KiB 14:30:01
2  54321   54323  std::vector::push_back()   2.50 MiB    5.00 MiB  500     0       2.50 MiB  10.2 KiB 64.0 KiB 14:30:02
...
[aggregator] 2 groups, 2 records | total: current=10.50 MiB, allocs=2500, frees=1000
```

### 输出字段说明

| 字段          | 说明                                          |
| ----------- | ------------------------------------------- |
| `#`         | 序号                                          |
| `PID`       | 目标进程 PID                                    |
| `TID`       | 线程 ID（`--group-by frame` 下显示 `(N threads)`） |
| `CallStack` | 内存分配的调用栈摘要（经 FrameFilter 过滤后的代表帧）           |
| `Current`   | 当前仍在使用中的内存                                  |
| `Total`     | 累计分配的内存总量                                   |
| `Allocs`    | 分配次数                                        |
| `Frees`     | 释放次数                                        |
| `Peak*`     | 历史峰值内存占用（聚合模式下为 max(per-record peak)，可能低估）  |
| `AvgSize`   | 平均分配大小                                      |
| `MaxSize`   | 最大单次分配大小                                    |
| `FirstSeen` | 首次出现时间                                      |

### 分组模式（--group-by）

`--group-by` 控制如何对内存记录进行聚合：

| 模式              | 说明                 | 适用场景          |
| --------------- | ------------------ | ------------- |
| `tid+frame`（默认） | 按 线程ID + 调用栈代表帧 聚合 | 需要区分不同线程的内存使用 |
| `frame`         | 仅按调用栈代表帧聚合，忽略线程    | 关注总体的内存热点     |
| `raw`           | 不聚合，显示每条原始记录       | 调试或需要完整信息     |

```bash
# 默认：按线程 + 调用栈聚合
sudo spdiag memstat --pid 54321 --group-by tid+frame

# 按调用栈聚合（合并所有线程）
sudo spdiag memstat --pid 54321 --group-by frame

# 显示原始记录
sudo spdiag memstat --pid 54321 --group-by raw
```

### TopN 控制（--top）

默认显示排序后的前 20 条记录，剩余记录汇总为 `[others]` 行：

```bash
# 只显示前 10 条
sudo spdiag memstat --pid 54321 --top 10

# 显示所有记录
sudo spdiag memstat --pid 54321 --top 999999
```

### 排序（--sort）

默认按 `current:desc,peak:desc,allocs:desc` 排序。支持多键排序和 `:asc`/`:desc` 方向：

| 排序键          | 说明                 | 默认方向 |
| ------------ | ------------------ | ---- |
| `current`    | 当前内存使用             | 降序   |
| `peak`       | 峰值内存               | 降序   |
| `total`      | 累计分配总量             | 降序   |
| `allocs`     | 分配次数               | 降序   |
| `frees`      | 释放次数               | 降序   |
| `leaked`     | 泄漏次数(allocs-frees) | 降序   |
| `avg_size`   | 平均分配大小             | 降序   |
| `max_size`   | 最大单次分配             | 降序   |
| `min_size`   | 最小单次分配             | 降序   |
| `first_seen` | 首次出现时间             | 升序   |
| `frame`      | 调用栈名称              | 升序   |

```bash
# 按泄漏量降序
sudo spdiag memstat --pid 54321 --sort leaked:desc

# 多键排序：先按 current 降序，再按 allocs 降序
sudo spdiag memstat --pid 54321 --sort current,allocs

# 按调用栈名称升序
sudo spdiag memstat --pid 54321 --sort frame:asc
```

### 过滤（--filter）

按调用栈代表帧名称进行通配符过滤（匹配任一模式即保留）：

```bash
# 只显示 Cache 相关条目
sudo spdiag memstat --pid 54321 --filter "*Cache*"

# 多模式过滤
sudo spdiag memstat --pid 54321 --filter "*alloc*,*new*"

# 组合排序与过滤
sudo spdiag memstat --pid 54321 --sort leaked:desc --filter "*leak*"
```

### 大小格式（--bytes）

```bash
# 默认人类可读格式，如 8.00 MiB
# 加 --bytes 切换为原始字节数，如 8388608
sudo spdiag memstat --pid 54321 --bytes
```

### 自定义列（--show）

通过 `--show` 指定只显示哪些数据列（PID、TID、CallStack 始终显示）：

```bash
# 只显示泄漏量和内存周转
sudo spdiag memstat --pid 54321 --show leaked,churn_rate

# 只显示尺寸分布
sudo spdiag memstat --pid 54321 --show min_size,max_size

# 只显示当前内存和峰值
sudo spdiag memstat --pid 54321 --show current,peak
```

| 字段           | 说明                     |
| ------------ | ---------------------- |
| `current`    | 当前仍在使用中的内存             |
| `total`      | 累计分配总量                 |
| `allocs`     | 分配次数                   |
| `frees`      | 释放次数                   |
| `peak`       | 峰值内存                   |
| `min_size`   | 最小单次分配大小               |
| `max_size`   | 最大单次分配大小               |
| `first_seen` | 首次出现时间                 |
| `leaked`     | 泄漏次数（allocs - frees）   |
| `avg_size`   | 平均分配大小（Total / Allocs） |
| `churn`      | 累计周转量（total - current） |
| `churn_rate` | 平均周转速率（churn / 总时长）    |
| `alloc_rate` | 平均分配速率（allocs / 总时长）   |

### 帧过滤与详细模式（--no-filter / --detail）

```bash
# 禁用帧过滤，显示原始调用栈
sudo spdiag memstat --pid 54321 --no-filter

# 详细模式：展示每条记录的完整调用栈树
sudo spdiag memstat --pid 54321 --detail
```

FrameFilter 会自动执行以下过滤：

- 移除内存映射相关噪声帧
- 折叠 STL 胶水代码（如 `std::allocator` 内部调用）
- 检测分配器函数帧
- 去除重复帧
- 选取最有意义的 HeaderFrame 作为代表

### 自定义 libc 路径

默认使用 `/usr/lib64/libc.so.6`。不同系统路径可能不同：

```bash
# Debian/Ubuntu 系统
sudo spdiag memstat --pid 54321 --libc-path /usr/lib/aarch64-linux-gnu/libc.so.6

# 指定自定义路径
sudo spdiag memstat --pid 54321 --libc-path /path/to/libc.so.6
```

### 自定义采集间隔

```bash
# 每 500ms 采集一次
sudo spdiag memstat --pid 54321 --interval 500

# 每 5 秒采集一次
sudo spdiag memstat --pid 54321 --interval 5000
```

### 完整 CLI 参数表

| 参数            | 短选项    | 类型     | 默认值                    | 说明                                                                                                                                           |
| ------------- | ------ | ------ | ---------------------- | -------------------------------------------------------------------------------------------------------------------------------------------- |
| `--pid`       | <br /> | int    | 必填                     | 目标进程 PID                                                                                                                                     |
| `--libc-path` | <br /> | string | `/usr/lib64/libc.so.6` | libc.so 路径                                                                                                                                   |
| `--interval`  | <br /> | uint   | 1000                   | 采集间隔（毫秒）                                                                                                                                     |
| `--bytes`     | <br /> | flag   | off                    | 显示原始字节数（默认人类可读格式）                                                                                                                            |
| `--group-by`  | <br /> | string | `tid+frame`            | 分组模式：`tid+frame` / `frame` / `raw`                                                                                                           |
| `--top`       | <br /> | int    | 20                     | 显示前 N 条记录                                                                                                                                    |
| `--show`      | <br /> | string | <br />                 | 只显示指定列（PID/TID/CallStack 始终显示）。可用：`current,total,allocs,frees,peak,min_size,max_size,first_seen,leaked,avg_size,churn,churn_rate,alloc_rate` |
| `--sort`      | `-s`   | string | `current,peak,allocs`  | 排序（`KEY[:DIR][,KEY[:DIR]]...`）                                                                                                               |
| `--filter`    | <br /> | string | 全部                     | 通配符过滤（`GLOB[,GLOB]...`）                                                                                                                      |
| `--no-filter` | <br /> | flag   | off                    | 禁用帧过滤                                                                                                                                        |
| `--detail`    | <br /> | flag   | off                    | 显示完整调用栈树                                                                                                                                     |
| `--csv`       | <br /> | string | —                      | CSV 落盘目录（所有输出同时生成 CSV 文件，可用 Excel 打开）                                                                                                      |

***

## 功能四：OB Cachestat 缓存命中率监控

OB Cachestat 基于 perf\_event 读取 CPU 缓存的 references 和 misses，可对运行中的进程进行无侵入式缓存命中率监控。

### 前置条件

Cachestat 使用 PMU/perf_event，不依赖 libc uprobe 或 BTF。需要 Linux aarch64 支持相应 PMU 事件，并具备 root、`CAP_PERFMON` 或系统策略允许的 perf_event 权限。

### 第1步：确认目标进程 PID

```bash
pidof my_program
```

### 第2步：运行 cachestat

```bash
sudo spdiag cachestat --pid <PID>
```

**输出示例：**

```
#  Level  References    Misses        HitRate(%)
1  L1-dcache    1,234,567      45,678        96.30
2  L1-icache    567,890        12,345        97.83
3  LLC     1,234,567      234,567       81.00
```

### 输出字段说明

| 字段           | 说明                                                                                          |
| ------------ | ------------------------------------------------------------------------------------------- |
| `#`          | 序号                                                                                          |
| `Level`      | 缓存级别（L1-dcache = L1 数据缓存, L1-icache = L1 指令缓存, LLC = 末级缓存, dTLB = L1D TLB, iTLB = L1I TLB, branch = 分支预测单元） |
| `References` | 缓存访问次数（增量）                                                                                  |
| `Misses`     | 缓存未命中次数（增量）                                                                                 |
| `HitRate(%)` | 缓存命中率 = (1 - Misses / References) × 100                                                     |

### 自定义缓存级别

默认监控 L1-dcache、L1-icache、LLC 三个级别（精确计数）。可通过 `--cache-levels` 指定监控级别，可选值：`L1-dcache`、`L1-icache`、`LLC`、`dTLB`、`iTLB`、`branch`。超过 3 个级别时内核自动 multiplexing 计数器：

```bash
# 只监控 L1-dcache 缓存
sudo spdiag cachestat --pid 54321 --cache-levels L1-dcache

# 监控全部 6 级（L1-dcache + L1-icache + LLC + dTLB + iTLB + branch）
sudo spdiag cachestat --pid 54321 --cache-levels L1-dcache,L1-icache,LLC,dTLB,iTLB,branch
```

### 完整 CLI 参数表

| 参数               | 短选项    | 类型     | 默认值                    | 说明                                       |
| ---------------- | ------ | ------ | ---------------------- | ---------------------------------------- |
| `--pid`          | <br /> | int    | 必填                     | 目标进程 PID                                 |
| `--interval`     | <br /> | uint   | 1000                   | 采集间隔（毫秒）                                 |
| `--cache-levels` | <br /> | CSV    | `L1-dcache,L1-icache,LLC`           | 监控的缓存级别，可选：L1-dcache, L1-icache, LLC, dTLB, iTLB, branch |
| `--delta`        | <br /> | flag   | off                    | 显示每秒增量而非累计值                              |
| `--csv`          | <br /> | string | —                      | CSV 落盘目录（所有输出同时生成 CSV 文件，可用 Excel 打开） |

***

## 功能五：MemPoint 点位级内存观测

MemPoint 通过 USDT + eBPF 实现点位级内存观测。你需要集成 MemPoint SDK 标记特定的内存对象，然后使用 CLI 查看每个标记点位的内存使用情况。

### 前置条件

需要 Linux、BTF、root 或相应 BPF/perf capabilities；当前 BPF 构建目标为 ARM64。此外还需要 `systemtap-sdt-devel`（提供 `sys/sdt.h`）。

依赖安装：

```bash
sudo dnf install -y systemtap-sdt-devel
```

### 第1步：选择 MemPoint Key

当前 MemPoint key 由 SpDiag SDK 的 `include/spdiag/ob_mempoint_keys.def` 统一生成。例如：

```cpp
MEM_KEY_DEF(RPC_REQUEST_BUF,  "Rpc", "Stack_value")
MEM_KEY_DEF(SQL_PLAN_CACHE,   "Sql", "Tracked_vector")
```

每行三个参数：枚举名、分类名、描述。业务可直接使用已发布 key；新增自定义 key 需要修改该文件并重新编译 SpDiag SDK、CLI 和业务程序，不能像 PerfPoint `.def` 那样只重编业务程序。

### 第2步：集成 SDK

```cpp
#include <spdiag/mem_point.h>
#include <spdiag/mem_point_keys.h>
```

### 第3步：标记内存对象

三种跟踪方式：

```cpp
using namespace SpDiag;

// 方式1：RAII 跟踪局部变量（自动触发 enter/leave）
{
    uint64_t buffer[128];
    auto mp = MemPoint::TrackValue(
        MemPointKey::RPC_REQUEST_BUF, buffer, sizeof(buffer));
    // 使用 buffer... 析构时自动触发 leave
}

// 方式2：跟踪动态容器（allocator 层自动追踪）
#include <spdiag/tracked_containers.h>
tracked::vector<int, MemPointKey::SQL_PLAN_CACHE> vec;
vec.reserve(1024);
vec.push_back(42);
vec.clear();  // 分配/释放自动触发探针

// 方式3：手动跟踪裸指针
auto buf = new char[4096];
{
    auto mp = MemPoint::TrackBuffer(
        MemPointKey::RPC_RESPONSE_BUF, buf, 4096);
    // ... 使用 buf ...
}  // mp 析构时触发 leave
delete[] buf;
```

### 第4步：编译

```cmake
find_package(SpDiag CONFIG REQUIRED)
target_link_libraries(my_app PRIVATE SpDiag::spdiag_lib)
```

必须先用 `bash build.sh -m on` 或 RPM 的 `bash build.sh package -m on` 构建 SpDiag。缺少 `sys/sdt.h` 时 CMake 会关闭该功能；未定义 `SPDIAG_ENABLE_MEMPOINT` 时相关类型不会声明，业务代码应使用同一构建配置或用条件编译保护。

### 第5步：运行与查看

```bash
# 终端1：启动你的程序
./my_app

# 终端2：查看 MemPoint 数据
sudo spdiag mempoint --pid $(pidof my_app)

# 按峰值排序，只看前 5
sudo spdiag mempoint --pid $(pidof my_app) --sort peak --top 5

# 过滤 SQL 相关
sudo spdiag mempoint --pid $(pidof my_app) --filter "Sql.*"

# 自定义列
sudo spdiag mempoint --pid $(pidof my_app) --show leaked,churn_rate,alloc_rate
```

### 完整 CLI 参数表

| 参数           | 短选项    | 类型     | 默认值            | 说明                                                                                                                                      |
| ------------ | ------ | ------ | -------------- | --------------------------------------------------------------------------------------------------------------------------------------- |
| `--pid`      | <br /> | int    | 必填             | 目标进程 PID                                                                                                                                |
| `--interval` | <br /> | uint   | 1000           | 采集间隔（毫秒）                                                                                                                                |
| `--bytes`    | <br /> | flag   | off            | 显示原始字节数（默认人类可读格式）                                                                                                                       |
| `--sort`     | `-s`   | string | `current,peak` | 排序（`KEY[:DIR][,KEY[:DIR]]...`，键与 memstat 共享）                                                                                            |
| `--filter`   | <br /> | string | 全部             | 通配符过滤（`GLOB[,GLOB]...`，匹配 `category.keyName`）                                                                                           |
| `--top`      | <br /> | int    | 20             | 显示前 N 条                                                                                                                                 |
| `--show`     | <br /> | string | <br />         | 自定义显示列（Category/Key 始终显示）。可用：`current,total,allocs,frees,peak,min_size,max_size,first_seen,leaked,avg_size,churn,churn_rate,alloc_rate` |
| `--csv`      | <br /> | string | —              | CSV 落盘目录（所有输出同时生成 CSV 文件，可用 Excel 打开） |

***

## 常见问题 FAQ

### Q：PerfPoint 打点对性能有多大影响？

PerfPoint 热路径操作为 O(1)，但实际耗时受 CPU、计时源、编译模式和已启用功能影响。请在目标环境运行 `benchmark_perfpoint_overhead` 测量，不应使用固定纳秒数作为跨平台承诺。

### Q：OB Memstat 需要修改目标程序吗？

不需要。OB Memstat 通过 eBPF uprobe 在内核层面拦截目标进程的 `malloc`/`calloc`/`realloc`/`free` 调用，完全无侵入。

### Q：多个程序可以同时使用 PerfPoint 吗？

可以。每个程序使用不同的 `programName` 注册并拥有独立 slot 范围；上限由共享内存配置中的 `maxPrograms` 决定，不应硬编码为固定值。

### Q：PerfPoint 和 OB 功能可以同时使用吗？

可以。两者是完全独立的。PerfPoint 基于共享内存，OB 基于内核态探针，互不干扰。

### Q：如何获取目标进程的 PID？

```bash
pidof my_program          # 按名称精确查找
pgrep my_program          # 按名称模式匹配
ps aux | grep my_program  # 进程列表过滤
```

### Q：memstat 的 CallStack 列显示的是十六进制地址？

这说明符号解析失败。确保目标程序编译时带有调试信息（`-g`），并且相关的 ELF 文件路径与 `/proc/<pid>/maps` 中的映射一致。你也可以使用 `--no-filter` 查看完整的原始调用栈。
