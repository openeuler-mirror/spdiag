# memstat_demo 使用指南

`memstat_demo` 是一个内存分配验证工具，用于配合 `spdiag memstat` 进行功能验证和问题诊断。它提供 5 种典型的内存分配模式，每种模式模拟不同的业务场景，产生可预测的分配行为。

## 快速开始

```bash
# 终端1：以 wait 模式启动 demo（打印 PID 后等待）
./build/examples/memstat_demo --mode leak --wait

# 终端2：运行 memstat 监控
sudo spdiag memstat --pid <PID>

# 回到终端1按 Enter 放行
# 按 Ctrl+C 停止 memstat 采集
```

## 5 种模式详解

### 1. normal — 基准对照

```bash
./memstat_demo --mode normal --count 200
```

**行为**：分配 N 个 1024 字节缓冲区，每个做 memset 写入和校验读取，然后全部释放。

**预期特征**：
- `current = 0`（所有分配都已释放）
- `allocs == frees`（完全配对）
- `min_size == max_size == avg_size == 1024 B`
- `total = allocs * 1024`
- 调用栈清晰简单，无异常帧

**业务场景**：正常的批量处理逻辑，如请求处理中分配临时缓冲区、批处理任务中的工作内存。

**诊断要点**：
- 如果 `current != 0` 或 `allocs != frees`，说明工具存在计数 bug
- 如果 size 统计不一致，说明工具的尺寸统计有误
- 此模式作为工具正确性的基准校验

**推荐 memstat 参数**：
```bash
sudo spdiag memstat --pid <PID> --show min_size,max_size,avg_size --group-by frame
```

---

### 2. leak — 内存泄漏

```bash
./memstat_demo --mode leak --count 200
```

**行为**：循环 N 次分配 2048 字节缓冲区。偶数次（正常路径）处理后释放；奇数次（异常路径）模拟 early return，忘记释放，导致泄漏。

**预期特征**：
- `current > 0`（约一半内存未释放）
- `allocs > frees`（frees ≈ allocs / 2）
- 泄漏量 ≈ N/2 * 2048 B（如 N=200 则泄漏约 200 KB）
- 责任帧（调用栈）有明确含义，可以看到"正常处理"和"异常跳过"两条路径

**业务场景**：真实代码中的异常路径泄漏，如：
- 错误处理分支忘记释放资源
- 提前 return 跳过清理逻辑
- 异常抛出时未执行 cleanup

**诊断要点**：
- 在 memstat 输出中，`leaked` 列应显示约 100（allocs=200, frees=100, leaked=100 次未释放）
- 关注 `allocs` 与 `frees` 的差值
- 通过调用栈定位泄漏的责任帧

**推荐 memstat 参数**：
```bash
sudo spdiag memstat --pid <PID> --show leaked,churn_rate --group-by frame --top 20
```

---

### 3. bimodal — 双模态分配

```bash
./memstat_demo --mode bimodal
```

**行为**：分配 10000 个 64 字节小块 + 1 个 64 MiB 大块，全部释放。

**预期特征**：
- `current = 0`（全部释放）
- `allocs == frees == 10001`
- 两个明显的尺寸簇：64 B（10000 次）和 64 MiB（1 次）
- `avg_size` 被大块拉高，不适合作为典型尺寸参考
- `total ≈ 10000 * 64 + 64 MiB ≈ 64.6 MiB`

**业务场景**：混合分配模式，如：
- 服务器同时处理大量小请求和偶尔的大文件传输
- 应用中使用对象池（小块）+ 偶尔的大缓冲区
- 数据库中的行缓存（小）+ 大查询结果集

**诊断要点**：
- 使用 `--show min_size,max_size,avg_size` 观察尺寸分布
- 关注 `max_size` 是否有异常大块
- 双模态分布是正常的，但如果大块频率异常增高则需要排查

**推荐 memstat 参数**：
```bash
sudo spdiag memstat --pid <PID> --show min_size,max_size,avg_size --group-by frame
```

---

### 4. churn — 高频抖动

```bash
./memstat_demo --mode churn
```

**行为**：紧凑循环中执行 100000 次分配/释放 256 字节。

**预期特征**：
- `current = 0`（每次立即释放）
- `allocs == frees == 100000`
- `total = 100000 * 256 = 25.6 MB`
- `churn` 为 `total - current`，累计周转字节数，永不归零
- `churn_rate` 反映平均周转速率（B/s）
- 单次分配的 `peak` 很低（仅 256 B）

**业务场景**：高频内存抖动，如：
- 请求处理中反复创建/销毁临时对象
- 解析器逐 token 分配缓冲区
- 日志格式化中的临时字符串拼接

**诊断要点**：
- 关注 `churn_rate` 列，高 churn_rate 表示频繁的内存分配/释放
- 虽然 `current = 0`（无泄漏），但高 churn_rate 会增加 GC 压力和内存碎片
- 如果 `churn_rate` 异常高，考虑使用对象池或预分配策略

**推荐 memstat 参数**：
```bash
sudo spdiag memstat --pid <PID> --show churn,churn_rate,alloc_rate --group-by frame --top 10
```

---

### 5. burst — 突发脉冲

```bash
./memstat_demo --mode burst
```

**行为**：休眠 2 秒后集中分配 100 个 4 KB 缓冲区，休眠 1 秒后全部释放。

**预期特征**：
- 分配前：`current = 0`
- 分配后释放前：`current ≈ 400 KB`
- 释放后：`current = 0`
- 时间线上呈现明显的"阶梯"模式
- `allocs == frees == 100`

**业务场景**：突发性内存使用，如：
- 定时任务批量加载缓存
- 批处理作业中的阶段性内存需求
- 报表生成时的临时数据聚合

**诊断要点**：
- 使用 `--interval 500` 缩短采集间隔以捕获突发模式
- 观察 `current` 列随时间的变化趋势
- 如果突发后 `current` 未回到 0，可能存在泄漏
- 关注 `peak` 与 `current` 的比值

**推荐 memstat 参数**：
```bash
sudo spdiag memstat --pid <PID> --interval 500 --show current,peak --group-by frame
```

---

## 模式对比总结

| 模式 | allocs vs frees | current | 泄漏 | churn | 典型尺寸 | 业务场景 |
|------|----------------|---------|------|-------|---------|---------|
| normal | allocs == frees | 0 | 无 | 低 | 固定 1024 B | 基准对照 |
| leak | allocs > frees | > 0 | 有 | 低 | 固定 2048 B | 异常路径泄漏 |
| bimodal | allocs == frees | 0 | 无 | 低 | 双簇 64 B / 64 MiB | 混合分配 |
| churn | allocs == frees | 0 | 无 | 高 | 固定 256 B | 高频抖动 |
| burst | allocs == frees | 0（释放后） | 无 | 中 | 固定 4 KB | 突发脉冲 |

## 常见诊断流程

### 诊断内存泄漏

```bash
# 1. 启动 leak 模式
./memstat_demo --mode leak --count 500 --wait

# 2. 在另一个终端监控（按泄漏量降序）
sudo spdiag memstat --pid <PID> --show leaked,current --group-by frame --sort leaked:desc --top 20

# 3. 按 Enter 放行，观察 leaked 列持续增长
```

### 诊断内存抖动

```bash
# 1. 启动 churn 模式
./memstat_demo --mode churn --wait

# 2. 在另一个终端监控
sudo spdiag memstat --pid <PID> --show churn,churn_rate,alloc_rate --group-by frame

# 3. 按 Enter 放行，观察 churn_rate 列的高值
```

### 验证工具正确性

```bash
# 1. 启动 normal 模式
./memstat_demo --mode normal --count 200 --wait

# 2. 在另一个终端监控
sudo spdiag memstat --pid <PID> --show min_size,max_size,avg_size,current --group-by frame

# 3. 按 Enter 放行
# 4. 验证：current 应为 0，所有尺寸应一致为 1024 B
```

### 排序与过滤

memstat 支持多键排序和通配符过滤，帮助快速定位内存问题：

```bash
# 按泄漏量降序，再按当前内存降序
sudo spdiag memstat --pid <PID> --sort leaked:desc,current:desc

# 只显示特定名称的调用栈
sudo spdiag memstat --pid <PID> --filter "*alloc*"

# 组合排序与过滤
sudo spdiag memstat --pid <PID> --sort current:desc --filter "*leak*" --top 10
```
