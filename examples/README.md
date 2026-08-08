## csv_export_demo - CSV 导出验证负载

`csv_export_demo` 是供 `scripts/verify_csv_export.sh` 和人工验证使用的轻量负载程序。它持续产生 `Compute.Sort`、`Memory.Alloc`、`IO.IOTask` 三类 PerfPoint，同时制造 CPU、缓存访问和内存分配活动。

Demo 本身不写 CSV。CSV 文件仍由 `spdiag <command> --csv <dir>` 生成，因此可以单独验证 CLI 的 CSV 导出链路。

```bash
# 编译
cmake --build build --target csv_export_demo

# 默认运行 60 秒
./build/examples/csv_export_demo

# 持续运行，直到收到 SIGINT 或 SIGTERM
./build/examples/csv_export_demo --duration 0 --threads 4

# 配合 CLI 手工验证
spdiag start --perflog
./build/examples/csv_export_demo --duration 60 &
spdiag show --csv ./results
spdiag show --perflog --csv ./results
```

***

## Demo 任务说明

Demo 启动 8 个线程，绑定到 Core 0\~6（其中 Core 0 共享 2 个线程），运行 7 类性能打点任务：

| 核心     | 模块.打点名              | 级别           | 任务内容                  | 设计目的                              |
| ------ | ------------------- | ------------ | --------------------- | --------------------------------- |
| Core 0 | Compute.MatMul      | KeyModule(2) | 32×32 矩阵乘法            | 测试 Bad/Not 计数（见下文）                |
| Core 0 | Memory.Alloc        | Debug(4)     | vector 分配/填充/释放       | **共享核心**验证（与 MatMul 竞争 shard）     |
| Core 1 | Compute.Prime       | KeyModule(2) | 素数统计 (100000\~100500) | CPU 密集型任务（**多核实例**）               |
| Core 2 | Compute.Prime       | KeyModule(2) | 素数统计 (100000\~100500) | CPU 密集型任务（**多核实例**）               |
| Core 3 | IO.IOTask           | KeyModule(2) | `sleep_for(5ms)`      | 模拟 IO 等待，使用 PerfKeys API          |
| Core 4 | Process.Work        | KeyModule(2) | busy-wait 精确 1ms      | 核实打点计时精准性                         |
| Core 5 | Pipeline.Stage1/2/3 | KeyModule(2) | 排序 → 查找 → 求和          | **单模块多打点**（1 module 贡献 3 个 point） |
| Core 6 | System.Scheduler    | SubSystem(1) | `sleep_for(2ms)`      | 低级别 (level=1) 任务                  |

**SDK 配置**：`levelGate = 4`（Debug 级别，所有打点均启用），`dumpIntervalMs = 500`。

#### Compute.MatMul — Bad/Not 计数测试

该任务通过 `rand() % 10` 引入随机行为，用于验证 `PerfStat` 中 `backGoodCount`、`backBadCount`、`backNotCount` 的正确性：

| rand() 结果 | 概率  | 行为                                             | 对应计数器    |
| --------- | --- | ---------------------------------------------- | -------- |
| 0         | 10% | 构造后调用 `pt.Start()`，不调用 `pt.End()`，让 `PerfPoint` 析构时触发 `Abandon()` | `Not++`  |
| 1         | 10% | 调用 `pt.End(1)`，返回非零错误码                         | `Bad++`  |
| 2\~9      | 80% | 调用 `pt.End(0)`，正常结束                            | `Good++` |

预期在足够多的迭代后，`Good : Bad : Not` 的比例应趋近 **8 : 1 : 1**。

#### Compute.Prime — 多核聚合验证

Prime 任务同时运行在 Core 1 和 Core 2 上，用于验证 SDK 的多核聚合能力。每个核心独立统计各自的 Tick/Min/Max 数据，`Aggregator` 按 `locationId` 自动合并：

- **RawTable（per-core）**：`Compute.Prime` 出现两行，分别对应 Core 1 和 Core 2
- **Summary（聚合）**：`Compute.Prime` 只出现一行，Ticks/Good/Total 为两核之和，Min 取两核最小值，Max 取两核最大值

#### Pipeline.Stage1/2/3 — 单模块多打点验证

Pipeline 任务在一个循环中依次经过 3 个阶段，每阶段使用独立的 `PerfPoint` 包裹。三个打点属于同一个 `"Pipeline"` 模块，用于验证：

- 同一模块下多个探针点的正确注册与统计
- 不同阶段耗时差异的可观测性（排序 > 求和 > 查找）

#### Memory.Alloc + Compute.MatMul — 共享核心验证

`Memory.Alloc`（Debug 级别）与 `Compute.MatMul`（KeyModule 级别）绑定在同一个 Core 0 上，用于验证：

- 多线程写入同一 `PerfShard` 时的数据隔离与正确性
- RawTable 中同一 Core 下不同探针的独立统计

#### Process.Work — 计时精准性验证

该任务每次迭代执行一个精确的 1ms busy-wait（基于 `std::chrono::steady_clock` 轮询），然后调用 `pt.End(0)`。由于每次耗时严格固定在 \~1ms，可以通过观察输出中的 **Avg(ns)** 是否接近 1,000,000ns、**Min/Max** 的波动范围来验证打点程序的计时精度。

***

## mempoint_demo — MemPoint SDK 演示

MemPoint 点位级内存观测演示程序。演示所有 MemPoint 跟踪模式，包括基础的 MemPoint RAII 跟踪和 ObjectMemPoint 字段级内存拆解。

### 编译与运行

```bash
# 编译（需要 Linux + systemtap-sdt-dev）
bash build.sh -t on -m on

# 运行所有演示模式
./build/examples/mempoint_demo --wait

# 运行指定模式
./build/examples/mempoint_demo --mode struct_breakdown --count 200

# 持续循环（配合 CLI 观测）
./build/examples/mempoint_demo --mode vector_growth --loop --interval 500

# 性能基准测试
./build/examples/mempoint_demo --mode benchmark --iterations 2000000
```

**配合 CLI 使用**：

```bash
# 终端 1：启动 demo，等待信号
./build/examples/mempoint_demo --mode struct_breakdown --wait

# 终端 2：启动 CLI 观测
spdiag mempoint --pid $(pidof mempoint_demo)

# 终端 2 观测到数据后，终端 1 按 Enter 开始
```

### 演示模式

#### MemPoint 基础跟踪

| 模式 | 说明 | MemPointKey |
|------|------|-------------|
| `stack_value` | TrackValue RAII 跟踪局部变量 | RPC_REQUEST_BUF |
| `tracked_vector` | tracked::vector 动态增长，自动 enter/leave | SQL_PLAN_CACHE |
| `tracked_string` | tracked::string append/assign | LOG_WRITE_BUFFER |
| `tracked_hashmap` | tracked::unordered_map insert/erase | SESSION_CONN_CTX |
| `raw_buffer` | TrackBuffer 裸指针 + 手动生命周期 | RPC_RESPONSE_BUF |
| `leak` | 故意泄漏（不析构），用于泄漏检测 | TX_UNDO_BUFFER |
| `struct_member` | POD 结构体外部 MemPoint guard | SQL_RESULT_SET |
| `heap_object` | RAII 类内嵌 MemPoint 成员 | TX_EXEC_CTX |
| `object_pool` | 预分配对象池 acquire/release | NET_SEND_BUF |
| `custom_lifecycle` | unique_ptr 队列延迟析构 | ASYNC_TASK_CTX |
| `vector_growth` | vector 倍增扩容 reallocation 模式 | SQL_VECTOR_GROW |
| `string_sso` | string SSO 边界跨越（短字符串 vs 堆分配） | LOG_STRING_SSO |
| `map_churn` | unordered_map 大量 insert/erase 抖动 | CONN_MAP_CHURN |
| `spike` | 短暂大块分配尖峰 | RPC_SPIKE |
| `nested` | 嵌套 vector\<string\> 多 key | SQL_NESTED_VEC / SQL_NESTED_STR |
| `realloc_cascade` | vector resize/shrink_to_fit 级联 | SQL_REALLOC |

#### ObjectMemPoint 字段级内存拆解

| 模式 | 说明 |
|------|------|
| `struct_breakdown` | 6 个场景演示 ObjectMemPoint 字段级拆解 |

`struct_breakdown` 模式包含 6 个场景，每个使用不同的 MemPointKey，CLI 输出中会显示所有 6 个：

| 场景 | 类型 | MemPointKey | 演示内容 |
|------|------|-------------|---------|
| PlainData | struct (POD) | RPC_REQUEST_BUF (1) | 纯值类型字段：int, double, bool |
| ContainerData | struct | SQL_PLAN_CACHE (3) | 容器字段：vector, string, map |
| SecretPayload | class (私有字段) | TX_EXEC_CTX (8) | 私有字段通过 TakeSnapshot() 成员函数注册 |
| NestedData | struct | SESSION_CONN_CTX (5) | 嵌套容器：vector\<string\>, unordered_map |
| SSOData | struct | LOG_WRITE_BUFFER (7) | SSO 字符串（短字符串堆大小为 0） |
| WideData | struct | SQL_RESULT_SET (4) | 多字段结构（11 个字段，接近 16 字段上限） |

**CLI 输出示例**：

```
Struct Memory Breakdown:

  ContainerData (total: 120 B)
  ├── id             4 B (static)
  ├── items         24 B (static) + 400 B (heap)
  ├── label         32 B (static) + 48 B (heap)
  ├── scores        48 B (static) + 176 B (heap)
  └── Padding        4 B
      Total: 120 B | Static: 108 B (90.0%) | Heap: 624 B (...)
```

### 命令行选项

| 选项 | 说明 | 默认值 |
|------|------|--------|
| `--mode <mode>` | 运行指定模式 | 全部模式 |
| `--count <N>` | 迭代次数 | 100 |
| `--repeat <N>` | 重复次数 | 1 |
| `--iterations <N>` | 基准测试迭代次数 | 1000000 |
| `--loop` | 持续循环运行 | 单次 |
| `--interval <ms>` | 循环间隔 | 500 |
| `--wait` | 等待 SIGUSR1 信号后开始 | 立即开始 |
| `--help, -h` | 显示帮助 | — |

### 性能基准测试

`benchmark` 模式测量单次 TrackValue 构造+析构的耗时：

```bash
./build/examples/mempoint_demo --mode benchmark --iterations 2000000
```

输出示例：
```
  [benchmark] TrackValue overhead: 5.2 ns/call (2000000 iterations, 10400 us total)
```
