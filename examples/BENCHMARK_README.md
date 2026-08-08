# SpDiag 性能测试套件使用指南

本文只描述当前 `examples/CMakeLists.txt` 中实际构建的性能测试程序。所有结果都与 CPU、编译类型、绑核状态、系统负载和 SpDiag 编译选项有关，不应把某个环境中的数值当作固定开销。

## 构建与前置条件

`build.sh` 默认构建 examples；做性能测量时建议使用 Release，并避免安装到系统：

```bash
bash build.sh -r -e on -i off
```

也可以直接使用 CMake：

```bash
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release -DBUILD_EXAMPLES=ON -DBUILD_TESTS=OFF
cmake --build build --parallel
```

PerfPoint 只有在共享内存存在且 SDK 成功注册后才会进入统计热路径。需要测量“启用采集”时的开销或用 `spdiag show` 查看数据时，先启动共享内存：

```bash
./build/src/cli/spdiag start
# 运行测试程序
./build/src/cli/spdiag stop
```

`build.sh` 在发现 `/dev/shm/spdiag_shm*` 仍存在时会要求先停止 SpDiag，避免新旧共享内存布局不一致。

## 当前基准程序

| 程序 | 用途 | 主要参数 |
|------|------|----------|
| `demo_app_unbound` | 多线程、不绑核的 PerfPoint 功能/迁核场景 | `-i <每线程迭代数>`，默认 10000；`-t <线程数>`，默认 8 |
| `benchmark_time` | 对比 baseline/“monitored”循环，并比较绑核与不绑核；当前实现有测量限制，见下文 | `-s/-m/-l <迭代数>`；`--bound`、`--unbound`、`--both`；`--core <N>` |
| `benchmark_memory` | RSS 与共享内存布局实验程序；当前初始化流程有测量限制，见下文 | `-q` 快速模式 |
| `benchmark_io` | 重复执行文件 IO、随机 IO、内存复制/填充任务；当前没有插入 PerfPoint | `-q`；`-i <迭代数>`，默认 1000 |
| `benchmark_perfpoint_overhead` | 直接循环执行同一个 PerfPoint 的 `Start()`/`End()` | `-n <迭代数>`，默认 1000000 |
| `benchmark_start_stop_overhead` | 对比共享内存启用/停用时的 `Start()`/`End()` 路径与空循环基线 | `-n <迭代数>`，默认 1000000；`-r <轮数>`，默认 5；`--core <N>`，默认 0 |

程序自己的帮助是参数定义的最终依据：

```bash
./build/examples/demo_app_unbound --help
./build/examples/benchmark_time --help
./build/examples/benchmark_memory --help
./build/examples/benchmark_io --help
./build/examples/benchmark_perfpoint_overhead --help
./build/examples/benchmark_start_stop_overhead --help
```

## 使用示例

### 1. 不绑核多线程示例

```bash
./build/src/cli/spdiag start
./build/examples/demo_app_unbound
./build/examples/demo_app_unbound -i 50000 -t 16
./build/src/cli/spdiag show --detail
./build/src/cli/spdiag stop
```

该程序按线程编号循环分配 8 类任务，线程不设置 CPU affinity，操作系统可以迁移线程。它按迭代数结束，不支持旧文档中的“运行秒数”或“按 Enter 停止”参数。

### 2. 时间开销对比

```bash
# 默认同时测试绑核和不绑核；绑核使用 core 0
./build/examples/benchmark_time

# 快速检查
./build/examples/benchmark_time -s 1000 -m 100 -l 10

# 只测试绑核，并指定 core 2
./build/examples/benchmark_time --bound --core 2

# 只测试不绑核
./build/examples/benchmark_time --unbound
```

默认迭代数为短任务 10000、中任务 1000、长任务 100。当前“monitored”循环构造 `PerfPoint` 后直接调用 `End(0)`，没有先调用 `Start()`；因此它不代表正常的 `Start()`/`End()` 埋点开销，输出的 overhead 只能用于观察当前程序两段循环的差异。差值还会受调度、频率变化和缓存状态影响，出现负差值并不代表 PerfPoint 有“负开销”。需要测量有效打点路径时应使用 `benchmark_perfpoint_overhead`。

### 3. 内存模型估算

```bash
./build/examples/benchmark_memory
./build/examples/benchmark_memory -q
```

快速模式当前列出 4 组配置，完整模式列出 13 组配置。需要注意当前程序为每组生成随机 SHM 名称并直接调用 `PerfManager::Init()`，但没有先用 CLI 创建该 SHM；初始化通常会失败。此外，组参数中的 `numCores` 和 `maxPoints` 没有写入传给 `PerfManager` 的 `PerfConfig`。只有初始化成功后程序才计算布局并读取 RSS，因此当前版本不适合作为有效的内存基准。其布局函数中的 `maxPoints` 只是模型输入，也不等同于 SDK 编译期常量 `MAX_PERF_SLOTS`（当前为 256）。

### 4. IO 场景

```bash
./build/examples/benchmark_io
./build/examples/benchmark_io -q
./build/examples/benchmark_io -i 500
```

`-q` 把基础迭代数设为 100；如果 `-q` 后又给出 `-i`，以命令行最后一次赋值为准。大文件顺序读写使用基础迭代数的十分之一，所以不要把 `-i` 设为小于 10。当前 baseline 和 “monitored” 两个循环都只调用同一个任务，第二段没有 PerfPoint，因此输出反映重复测量波动，不是 SpDiag 开销。

### 5. PerfPoint 循环开销

```bash
./build/src/cli/spdiag start
./build/examples/benchmark_perfpoint_overhead
./build/examples/benchmark_perfpoint_overhead -n 5000000
./build/src/cli/spdiag stop
```

该程序先预热 100 次，再统计循环中 `Start()` + `End()` 的平均耗时。输出中的英文结论写着 “constructor + End”，但当前计时区间实际上不包含 `PerfPoint` 构造函数；构造发生在预热和计时循环之前。

### 6. 启用与停用采集的路径对比

`benchmark_start_stop_overhead` 将同一个 `PerfPoint` 的 `Start()`/`End()` 循环与不调用 PerfPoint 的基线循环对比，并对多轮结果取中位数。共享内存状态必须在启动程序前确定：

```bash
# Case A：共享内存存在，测量采集启用路径
./build/src/cli/spdiag start
./build/examples/benchmark_start_stop_overhead -n 5000000 -r 7 --core 0
./build/src/cli/spdiag stop

# Case B：共享内存不存在，测量快速返回路径
./build/examples/benchmark_start_stop_overhead -n 5000000 -r 7 --core 0
```

两种状态需要分别启动一次测试程序；不要在同一进程运行期间用 `start`/`stop` 切换后直接比较已有 `PerfPoint` 对象。

## 推荐的测量方法

为提高可重复性：

1. 使用 Release 构建，并记录编译选项（特别是 `ENABLE_PERCENTILE`、`ENABLE_PERFLOG`）。
2. 分别记录共享内存不存在和 `spdiag start` 后的结果；前者会走未启用的快速返回路径，不能代表采集开启时的开销。
3. 绑核测试前确认目标 core 在线且允许设置 affinity；容器或受限环境可能拒绝绑核。
4. 每组至少运行多次，报告中位数和离散范围，不只保留单次结果。
5. 同时记录 CPU 型号、内核、编译器、线程数、迭代数和系统负载。

一个最小的人工回归流程：

```bash
bash build.sh -r -e on -i off
./build/src/cli/spdiag start
./build/examples/benchmark_perfpoint_overhead -n 5000000
./build/src/cli/spdiag stop
```

`benchmark_time`、`benchmark_io` 和 `benchmark_memory` 当前存在上述实现限制，不应作为性能门禁；修复对应源码后再把它们加入自动回归。

仓库当前没有 `examples/run_benchmarks.py`，也不会自动生成 HTML、PNG 或 JSON 报告。需要在 CI 中保存结果时，可直接重定向各程序的标准输出，由 CI 归档文本文件。

## 常见问题

### 找不到测试程序

确认配置时启用了 examples：

```bash
cmake -S . -B build -DBUILD_EXAMPLES=ON
cmake --build build --parallel
```

### `spdiag show` 没有数据

必须先执行 `spdiag start`，再启动测试程序。程序退出前 SDK 会做最后一次 dump；长时间运行的程序也会按共享内存中的 `dumpIntervalMs` 周期转储。

### 结果波动很大

优先使用 Release、增加迭代数、减少后台负载，并分别比较 `benchmark_time --bound` 与 `--unbound`。不要沿用旧文档中的固定评级阈值；纳秒级差值应结合重复实验和业务路径耗时判断。
