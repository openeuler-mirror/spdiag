# cachestat_demo 使用指南

`cachestat_demo` 是一个缓存命中率验证工具，用于配合 `spdiag cachestat` 进行功能验证和性能分析。它通过三种不同的矩阵乘法访问模式，产生可预测的缓存命中/未命中行为。

## 测量方法


```bash
# 1. 启动 demo（--wait 模式等待）
./build/examples/cachestat_demo --mode col-major --n 1024 --wait

# 2. 在另一终端启动 spdiag
sudo spdiag cachestat --pid 12345
# spdiag 开始每秒输出一行

# 3. 回到终端1按 Enter
# spdiag 开始捕获 cache 数据

# 4. demo 运行结束后（约 10-25 秒），Ctrl+C 停止 spdiag
```

## 命令行选项

```
Usage: cachestat_demo [options]

Options:
  --mode <mode>     Access mode: row-major, col-major, blocked (default: blocked)
  --n <N>           Matrix size N (default: 1024)
  --tile <T>        Tile size for blocked mode (default: 64)
  --threads <N>     Number of worker threads (default: 1)
  --repeat <N>      Number of repeats, median reported (default: 5)
  --core <C>        CPU core to bind (default: 0)
  --wait            Wait for ENTER before starting (for profiler attach)
  --perf-extra      Also measure CPU cycles and instructions
  --csv <path>      Output CSV file
  --help, -h        Show this help
```

## 3 种模式详解

### 1. row-major — 行优先访问

```bash
./cachestat_demo --mode row-major --n 1024
```

**循环顺序**：`for i → for k → for j`，内层 `C[i][j] += A[i][k] * B[k][j]`

**访问特征**：
- A 矩阵按行顺序读取（`A[i][k]`，k 递增），连续访问，cache line 利用好
- B 矩阵按列方向读取（`B[k][j]`，k 递增），每个元素跨一行，但 j 维度连续时 cache line 可覆盖多个元素
- C 矩阵按行写入，连续访问

**业务场景**：顺序遍历数组，如：
- 图像处理中的逐行扫描
- 音视频编码中的帧数据处理
- 矩阵运算中的标准实现

---

### 2. col-major — 列优先访问（高 miss 模式）

```bash
./cachestat_demo --mode col-major --n 1024
```

**循环顺序**：`for j → for i → for k`，内层 `sum += A[i][k] * B[k][j]`

**访问特征**：
- A 矩阵按行读取（`A[i][k]`，k 递增），连续访问
- **B 矩阵跨行跳跃读取**（`B[k][j]`，k 递增，stride = N * sizeof(double) = 8 KB），几乎每次访问触发 cache line miss
- 当 N = 1024 时，stride = 8 KB，远超 cache line 大小（64 B），空间局部性极差

**业务场景**：跨步访问，如：
- 数据库索引查找中的随机访问
- 矩阵转置操作
- 图处理中的邻接表遍历
- 哈希表冲突链遍历

---

### 3. blocked — 分块访问（最优模式）

```bash
./cachestat_demo --mode blocked --n 1024 --tile 64
```

**循环顺序**：tiled `for ii → for jj → for kk → for i → for k → for j`

**访问特征**：
- 将大矩阵分割为 tile × tile 的小块（默认 64×64 = 32 KB，可放入 L1 D-cache）
- 每个 tile 内的 A、B、C 数据在 L1 中完全复用
- 分块消除了原始矩阵乘法中的 cache 抖动

**业务场景**：缓存友好的算法设计，如：
- 分块矩阵运算
- 缓存友好的数据结构（B-tree、分块哈希）
- 图像分块处理（tile-based rendering）
- 数据库的页式存储设计

---

## 预期测试结果

### 按平台选择矩阵规模

不同平台的 cache 容量差异很大，需要选择合适的 N 使得工作集（3 × N² × 8 字节）超过目标 cache 层级：

| 目标 cache 层 | 需要工作集 > | 最小 N | 推荐 N |
|--------------|-------------|--------|--------|
| L1-dcache 区分 | 3×L1-dcache | 取决于 L1-dcache | 见下方说明 |
| LLC 区分 | 3×LLC | √(LLC/24) × 1.1 | 向上取 2 的幂 |

**L1-dcache 区分说明**：L1-dcache miss 由访问步长决定（col-major 步长 = N×8 字节），而非总工作集。

### N=3072 — 鲲鹏 920 推荐

工作集 216MB，超出 L3（128MB）。单次 matmul 约 20 秒，`--repeat 3` 约 1 分钟。**鲲鹏 920 上推荐用 N=3072 做验证。**

| 模式 | L1-dcache miss rate | 说明 |
|------|--------------|------|
| **blocked** | < 1% | tile 在 L1-dcache/L2 内复用 |
| **row-major** | < 1% | 顺序访问有 spatial locality |
| **col-major** | 1-5% | stride 访问，miss rate 最高 |

**关键指标**：L1-dcache miss rate 是主要区分指标。col-major 应为 blocked 的 10 倍以上。

**LLC miss rate 注意**：LLC (L3) miss rate 受比率悖论影响（见下方实测数据分析），不适合作为三种模式的区分指标。

### N=1024 — 小 cache 平台参考（L1D=64KB, LLC=6-12MB）

适用于传统 cache 容量的平台（x86 或小 cache ARM）。

| 模式 | L1-dcache miss rate | LLC miss rate | dTLB miss rate |
|------|--------------|--------------|----------------|
| **blocked** | 1-2% | 0.5-1% | < 0.1% |
| **row-major** | 3-5% | 1-3% | < 0.1% |
| **col-major** | 8-15% | 5-15% | < 1% |

### ARM 平台注意事项

- **dTLB / iTLB / branch**：鲲鹏 920 有 6 个可编程 PMU 计数器，每个缓存级别需要 2 个事件（access + miss）。默认 L1-dcache + L1-icache + LLC 占满 6 个计数器（无 multiplexing）。dTLB、iTLB、branch 可通过 `--cache-levels` 按需启用，超出计数器数量时内核自动 multiplexing 并由代码缩放
- **PMU 计数器数量**：ARM 通常 2-6 个可编程计数器。Demo 默认只开 4 个（L1-dcache×2 + LLC×2），不超过硬件上限
- **硬件预取**：ARM 处理器的硬件预取器会降低 col-major 的 L1-dcache miss rate，这是正常现象


### 内外对照验证

#### Demo 自测量（内观测）

Demo 进程自己通过 `perf_event_open` 创建 PMU 计数器，测量矩阵乘法期间的 cache 事件。测量流程：

```
1. 分配并填充矩阵（不计入测量）
2. Reset + Start 计数器
3. 执行矩阵乘法 ← 只测这一段的 cache 行为
4. Stop 计数器
5. 读取 scaled 值（含 multiplexing 校正）
```

关键设计：
- 矩阵分配和填充在计数器启动**之前**完成，避免 malloc/page fault 的 cache 事件污染测量
- 默认只开 4 个计数器（L1-dcache access/miss + LLC access/miss），不超过 ARM PMU 硬件计数器数量，避免 multiplexing
- 设置 `read_format = PERF_FORMAT_TOTAL_TIME_ENABLED | PERF_FORMAT_TOTAL_TIME_RUNNING`，读取 24 字节 `{value, time_enabled, time_running}` 并做 multiplexing 缩放
- 单线程模式下计算在主线程内联执行，计数器 `pid=0` 精确追踪当前线程
- dTLB/cycles/instructions 为可选计数器，仅在硬件支持时显示

#### spdiag cachestat 外观测（对照）

`spdiag cachestat` 是独立进程，通过 `perf_event_open` 附加到目标进程的所有线程，每秒采集一次增量数据：

```
1. 读取 /proc/<pid>/task/ 枚举所有线程 TID
2. 为每个 TID 打开 cache level 的 access/miss fd
3. 每秒 Collect：read fd → 缩放 → 聚合所有线程 → 计算增量 hit rate
4. 输出表格
```

## 推荐参数组合

```bash
# 鲲鹏 920 标准验证（N=3072，超出 L3=128MB，约 1 分钟/模式）
./cachestat_demo --mode col-major --n 3072 --repeat 3 --wait
./cachestat_demo --mode row-major --n 3072 --repeat 3 --wait
./cachestat_demo --mode blocked --n 3072 --repeat 3 --wait
# 配合：sudo spdiag cachestat --pid <PID>

# 快速验证（N=1024，适合小 cache 平台如 L1D=64KB/LLC=6-12MB）
./cachestat_demo --mode col-major --n 1024 --repeat 3

# 使用增量模式（每秒刷新，适合实时观察）
sudo spdiag cachestat --pid <PID> --delta

```

## 常见诊断流程

### 诊断业务程序的缓存性能

1. 用 col-major 模式作为"差"的基线，blocked 模式作为"优"的基线
2. 对比业务程序在相同矩阵规模下的 hit rate
3. 如果业务程序的 hit rate 接近 col-major，说明存在缓存不友好的访问模式
4. 参考 blocked 模式的分块策略优化业务代码

### 常见问题

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| dTLB 显示 "not supported" | ARM 平台不支持 `PERF_COUNT_HW_CACHE_DTLB` | 正常现象，跳过 dTLB 验证 |
| 三种模式 miss rate 相近 | VM 环境无 PMU passthrough | 在物理机或 PMU passthrough VM 上运行 |
| miss rate 全部为 0 | `perf_event_paranoid` 过高 | `sudo sysctl kernel.perf_event_paranoid=1` |
| spdiag 无数据输出 | 目标进程运行时间短于采样间隔 | 增大 `--n` 或减小 `--interval` |
