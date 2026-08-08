# mempoint_demo 使用指南

`mempoint_demo` 是一个 MemPoint SDK 演示工具，用于配合 `spdiag mempoint` 验证点位级内存观测功能。它提供 17 种模式，每个模式使用独立的 MemPointKey，Key 列直接显示模式名称，一目了然。

## 快速开始

```bash
# 终端1：以 wait 模式启动 demo（打印 PID 后等待）
./build/examples/mempoint_demo --wait

# 终端2：运行 mempoint 监控
sudo spdiag mempoint --pid <PID>

# 回到终端1按 Enter 放行
# 按 Ctrl+C 停止 mempoint 采集
```

## 命令行选项

```
Usage: mempoint_demo [options]

Modes:
  stack_value     TrackValue RAII for local variables
  tracked_vector  tracked::vector dynamic growth
  tracked_string  tracked::string append/assign
  tracked_hashmap tracked::unordered_map insert/erase
  raw_buffer      TrackBuffer raw pointer + manual lifecycle
  leak            Intentional leak (no destructor)
  struct_member   POD struct with external MemPoint guard
  heap_object     RAII class with embedded MemPoint member
  object_pool     Pre-allocated pool with acquire/release
  custom_lifecycle Delayed destruction via unique_ptr queue
  vector_growth   Vector doubling reallocation (no reserve)
  string_sso      String SSO boundary crossing
  map_churn       Map mass insert/erase churn
  spike           Brief 10 MiB allocation spike
  nested          Nested vector<string> (dual key)
  realloc_cascade Vector resize/shrink_to_fit cascade
  benchmark       Measure single TrackValue overhead

Options:
  --mode <mode>      Run single mode (default: all)
  --count <N>        Iteration count (default: 100)
  --repeat <N>       Repeat count (default: 1)
  --iterations <N>   Benchmark iterations (default: 1000000)
  --loop             Run in continuous loop
  --interval <ms>    Loop interval (default: 500)
  --wait             Wait for ENTER before starting
  --help, -h         Show this help
```

## Key 分配表

每个 demo 模式使用独立的 MemPointKey，运行 all modes 时输出 17 行（无碰撞）：

| Mode | Category | Key | SDK API |
|------|----------|-----|---------|
| stack_value | Rpc | Stack_value | TrackValue |
| raw_buffer | Rpc | Raw_buffer | TrackBuffer |
| spike | Rpc | Spike | TrackedAllocator (tracked::vector) |
| tracked_vector | Sql | Tracked_vector | TrackedAllocator (tracked::vector) |
| struct_member | Sql | Struct_member | TrackObject + heap guard |
| vector_growth | Sql | Vector_growth | TrackedAllocator (tracked::vector, no reserve) |
| nested | Sql | Nested_vec | TrackedAllocator (tracked::vector) |
| nested | Sql | Nested_str | TrackedAllocator (tracked::string) |
| realloc_cascade | Sql | Realloc_cascade | TrackedAllocator (tracked::vector) |
| tracked_hashmap | Connection | Tracked_hashmap | TrackedAllocator (tracked::unordered_map) |
| map_churn | Connection | Map_churn | TrackedAllocator (tracked::unordered_map) |
| leak | Transaction | Leak | TrackBuffer + heap guard (leaked) |
| tracked_string | Log | Tracked_string | TrackedAllocator (tracked::string) |
| string_sso | Log | String_sso | TrackedAllocator (tracked::string) |
| heap_object | Execution | Heap_object | RAII class member |
| object_pool | Net | Object_pool | TrackObject + pool |
| custom_lifecycle | Async | Custom_lifecycle | TrackObject + delayed destruct |
| benchmark | Rpc | Stack_value | TrackValue (standalone) |

## 预期输出（all modes, --count 100）

输出按 Current 降序排列，行号可能因运行环境不同而变化，以 Category/Key 定位。

```
    #  Category    Key                       Current      Total      Allocs       Frees       Peak    AvgSize    MaxSize  FirstSeen
 ----  ----------  ----------------------  ---------  ---------  ----------  ----------  ---------  ---------  ---------  ---------
    1  Transacti…  Leak                      400 KiB    400 KiB         100           0    400 KiB   4.00 KiB   4.00 KiB   HH:MM:SS
    2  Rpc         Spike                         0 B   10.0 MiB           1           1   10.0 MiB   10.0 MiB   10.0 MiB   HH:MM:SS
    3  Sql         Struct_member                 0 B   26.6 KiB         100         100   26.6 KiB      272 B      272 B   HH:MM:SS
    4  Execution   Heap_object                   0 B   18.8 KiB         100         100   18.8 KiB      192 B      192 B   HH:MM:SS
    5  Sql         Nested_str                    0 B   9.86 KiB         100         100   9.86 KiB      101 B      101 B   HH:MM:SS
    6  Sql         Realloc_cascade               0 B   14.1 KiB           4           4   9.77 KiB   3.52 KiB   7.81 KiB   HH:MM:SS
    7  Net         Object_pool                   0 B   50.8 KiB         100         100   4.06 KiB      520 B      520 B   HH:MM:SS
    8  Sql         Nested_vec                    0 B   3.12 KiB           1           1   3.12 KiB   3.12 KiB   3.12 KiB   HH:MM:SS
    9  Connection  Map_churn                     0 B   4.12 KiB         154         154   2.55 KiB       27 B    1016 B   HH:MM:SS
   10  Connection  Tracked_hashmap               0 B   3.34 KiB         104         104   2.55 KiB       32 B    1016 B   HH:MM:SS
   11  Rpc         Raw_buffer                    0 B    200 KiB         100         100   2.00 KiB   2.00 KiB   2.00 KiB   HH:MM:SS
   12  Log         String_sso                    0 B   1.85 KiB           6           6   1.41 KiB      316 B      961 B   HH:MM:SS
   13  Log         Tracked_string                0 B   1.85 KiB           6           6   1.41 KiB      316 B      961 B   HH:MM:SS
   14  Sql         Vector_growth                 0 B     1020 B           8           8      768 B      127 B      512 B   HH:MM:SS
   15  Sql         Tracked_vector                0 B      400 B           1           1      400 B      400 B      400 B   HH:MM:SS
   16  Async       Custom_lifecycle              0 B   3.12 KiB         100         100      352 B       32 B       32 B   HH:MM:SS
   17  Rpc         Stack_value                   0 B      800 B         100         100        8 B        8 B        8 B   HH:MM:SS
```

逐行解读：
- **Transaction/Leak**: 100 × 4 KiB leaked, Frees=0
- **Rpc/Spike**: 10 MiB 瞬态分配后 shrink_to_fit 释放, Allocs=1, Peak=10 MiB
- **Sql/Struct_member**: 100 × 272 B RawEntry, batch create then release
- **Execution/Heap_object**: enter=64B × 100, resize 每个增长到 192B, MaxSize=192B（resize 更新）, Total=6400+12800=19200
- **Sql/Nested_str**: 100 × string(100,'x') → 每个 101B 堆分配（100 chars + null）
- **Sql/Realloc_cascade**: 4 次 allocate（resize 100→1000, shrink_to_fit, resize 2000）, Peak=9.77K（realloc 期间新旧块叠加）
- **Net/Object_pool**: 100 × 520 B pooled, max 8 concurrent
- **Sql/Nested_vec**: reserve(100) → 1 次 allocate(100×sizeof(string)=3200B)
- **Connection/Map_churn**: 100 insert + 90 erase + 50 reinsert → 154 allocs（含 bucket rehashes）
- **Connection/Tracked_hashmap**: 100 insert + 50 erase → 104 allocs（每个节点独立堆分配 + bucket rehashes）
- **Rpc/Raw_buffer**: 100 × 2048 B, instant enter/leave
- **Log/String_sso**: "hi" + 100 × append("chunk_i ") → 6 次堆再分配（SSO 溢出后翻倍增长）
- **Log/Tracked_string**: 100 × append("chunk_i ") → 与 String_sso 相同模式（巧合：循环体相同，最终长度触发相同的再分配序列）
- **Sql/Vector_growth**: 8 次 allocate（capacity 1→2→4→8→16→32→64→128）, Total=1020B
- **Sql/Tracked_vector**: reserve(100) → 1 次 allocate(400B), Peak=400B
- **Async/Custom_lifecycle**: sizeof(RequestContext)=32B, Peak=352B（短暂 11 个并发）
- **Rpc/Stack_value**: 100 × 8 B uint64_t, instant enter/leave, Peak=8B（同时只有 1 个活对象）

## 17 种模式详解

### 1. stack_value — 栈变量 RAII 追踪

```bash
./mempoint_demo --mode stack_value --count 200
```

**行为**：循环 N 次，每次在栈上创建 `uint64_t` 变量，通过 `MemPoint::TrackValue` 追踪。enter/leave 在同一条语句内完成。

**SDK API**：`MemPoint::TrackValue(key, &value, sizeof(value))`

**点位**：`Rpc / Stack_value`

**预期特征**：
- `allocs == frees == N`
- `current = 0`
- `total = N × 8`
- `peak = 8 B`，`max_size == min_size == 8 B`

**业务场景**：请求处理中的局部缓冲区追踪。

---

### 2. tracked_vector — 动态数组增长追踪

```bash
./mempoint_demo --mode tracked_vector --count 200
```

**行为**：创建 `tracked::vector<int, SQL_PLAN_CACHE>`，先 `reserve(N)` 再循环 `push_back`，最后 `clear`。allocator 层自动触发 enter/leave 探针。

**SDK API**：`TrackedAllocator` — STL allocator 自动驱动

**点位**：`Sql / Tracked_vector`

**预期特征**：
- `current = 0`，`allocs == 1`
- `peak = N × sizeof(int)`
- `total = N × sizeof(int)`（allocator enter 精确追踪）

**业务场景**：SQL 执行计划缓存的动态扩容。

---

### 3. tracked_string — 字符串累积追踪

```bash
./mempoint_demo --mode tracked_string --count 200
```

**行为**：创建 `tracked::string<LOG_WRITE_BUFFER>`，循环 `append` 字符串片段，然后 `assign` 重置，最后 `clear`。allocator 自动追踪。

**SDK API**：`TrackedAllocator` — STL allocator 自动驱动

**点位**：`Log / Tracked_string`

**预期特征**：
- `current = 0`
- `allocs ≈ log₂(final_length / SSO_size)`（每次堆再分配触发 1 次 enter）
- `total` = 所有堆分配字节数之和（含已释放的中间 buffer）
- `peak` 反映 Append 过程中最大 capacity（含 null terminator）

**业务场景**：日志缓冲区的累积与刷新。

---

### 4. tracked_hashmap — 哈希表增删追踪

```bash
./mempoint_demo --mode tracked_hashmap --count 200
```

**行为**：创建 `tracked::unordered_map<int,int,SESSION_CONN_CTX>`，循环插入 N 个键值对，然后删除前 N/2 个。allocator 自动追踪。

**SDK API**：`TrackedAllocator` — STL allocator 自动驱动

**点位**：`Connection / Tracked_hashmap`

**预期特征**：
- `current = 0`（析构后归零）
- `allocs ≈ N + bucket_rehashes`（每个节点是独立堆分配，bucket rehash 也会触发 allocate/deallocate）
- `peak` 反映最大并发分配量（节点 + bucket 数组）
- `max_size` 反映最大的单次分配（bucket 数组远大于单个节点）

**业务场景**：会话连接的建立/断开管理。

---

### 5. raw_buffer — 原始指针手动生命周期

```bash
./mempoint_demo --mode raw_buffer --count 200
```

**行为**：循环 N 次，每次 `new char[2048]`，用 `TrackBuffer` 追踪，使用后 `delete[]`。

**SDK API**：`MemPoint::TrackBuffer(key, buf, size)`

**点位**：`Rpc / Raw_buffer`

**预期特征**：
- `allocs == frees == N`，`current = 0`
- `total = N × 2048`，`peak = 2 KiB`
- `max_size == min_size == 2048 B`

**业务场景**：手动管理的 RPC 响应缓冲区。

---

### 6. leak — 内存泄漏检测

```bash
./mempoint_demo --mode leak --count 200 --loop
```

**行为**：循环 N 次，每次 `new char[4096]`，然后 `new MemPoint(TrackBuffer(...))` 将 guard 堆分配。guard 永远不会析构。

**SDK API**：`new MemPoint(MemPoint::TrackBuffer(key, buf, size))` — 移动语义制造泄漏

**点位**：`Transaction / Leak`

**预期特征**：
- `allocs == N`，`frees == 0`
- `current = N × 4 KiB`（持续增长）
- 底部显示 **Leak Detection** 区域

**业务场景**：异常路径的资源泄漏检测。

---

### 7. struct_member — POD 结构体外部追踪

```bash
./mempoint_demo --mode struct_member --count 200 --loop
```

**行为**：创建 POD 结构体（272 B），通过堆分配的 `MemPoint` guard 追踪。批量创建后统一释放。

**SDK API**：`new MemPoint(MemPoint::TrackObject(key, obj))` + `delete guard`

**点位**：`Sql / Struct_member`

**预期特征**：
- `allocs == frees == N`
- `total = N × 272 B`，`peak = N × 272 B`
- `max_size == min_size == 272 B`

**业务场景**：C 风格结构体的外部追踪管理。

---

### 8. heap_object — RAII 类内嵌追踪

```bash
./mempoint_demo --mode heap_object --count 200 --loop
```

**行为**：创建 `TxContext` RAII 类（含 `MemPoint` 成员），`AppendUndo` 动态增长追踪大小。

**SDK API**：RAII 类 `MemPoint` 成员 + `UpdateSize`

**点位**：`Execution / Heap_object`

**预期特征**：
- `allocs == N`，`frees == N`
- `total = N × (sizeof(TxContext) + undo_capacity)`（enter + resize growth）
- `peak = N × (sizeof(TxContext) + undo_capacity)`（批量创建后全部存活）
- `max_size = sizeof(TxContext) + undo_capacity`（resize 更新 max_size）
- `min_size = sizeof(TxContext)`（enter 时的初始大小）

**业务场景**：事务上下文的 RAII 自动内存追踪。

---

### 9. object_pool — 对象池复用追踪

```bash
./mempoint_demo --mode object_pool --count 200 --loop
```

**行为**：预分配 8 个缓冲区槽位，循环 acquire/release，每次创建/销毁 MemPoint guard。

**SDK API**：`new MemPoint(TrackObject(key, obj))` + `delete guard`

**点位**：`Net / Object_pool`

**预期特征**：
- `allocs == frees == N`
- `total = N × 520 B`
- `peak ≤ 8 × 520 B`

**业务场景**：网络发送缓冲区池化复用。

---

### 10. custom_lifecycle — 延迟析构队列

```bash
./mempoint_demo --mode custom_lifecycle --count 200 --loop
```

**行为**：unique_ptr 队列持有请求上下文，FIFO 上限 10 个。超限后释放最早请求。

**SDK API**：`new MemPoint(TrackObject(key, obj))` + `delete guard`

**点位**：`Async / Custom_lifecycle`

**预期特征**：
- `allocs == N`，`frees == N`
- `total = N × sizeof(RequestContext)` = `N × 32 B`（8 B reqId + 24 B vector 头）
- `peak ≤ 11 × 32 B = 352 B`（入队瞬间可能短暂 11 个并发：新请求先入队，再检查超限弹出）

**业务场景**：异步请求处理的滑动窗口管理。

---

### 11. benchmark — 探针开销测量

```bash
./mempoint_demo --mode benchmark --iterations 1000000
```

**行为**：热身后循环 N 次 TrackValue，测量总耗时和平均单次开销。

**预期特征**：
- USDT 未激活：avg ≤ 5 ns
- USDT 激活：avg ≤ 2000 ns

---

### 12. vector_growth — 矢量倍增扩容

```bash
./mempoint_demo --mode vector_growth --count 1000
```

**原理**：不使用 `reserve`，直接 `push_back` N 个 `int`。STL vector 按 2 的幂次扩容：每次容量不足时，allocate 新块（2× 容量），拷贝元素，deallocate 旧块。TrackedAllocator 在每次 allocate/deallocate 自动触发 enter/leave 探针。

**行为**：
1. `push_back(0)` → allocate(1×int=4B)
2. `push_back(1)` → allocate(2×int=8B), deallocate(1×int=4B)
3. `push_back(2..3)` → allocate(4×int=16B), deallocate(2×int=8B)
4. ...直到 capacity ≥ N

**SDK API**：`tracked::vector<int, MemPointKey::SQL_VECTOR_GROW>`

**点位**：`Sql / Vector_growth`

**预期特征**（N=1000, sizeof(int)=4）：
- `allocs ≈ log₂(N) + 1 ≈ 11`（10 次扩容 allocate + 析构时 1 次 deallocate）
- 实际上每次扩容是 1 次 allocate + 1 次 deallocate，析构时还有 1 次 deallocate
- `max_size` = 最终容量的字节数（capacity=1024, max_size=4096B）
- `min_size` = 首次分配（1×sizeof(int)=4B）
- `current = 0`（函数结束后 vector 析构）
- `peak` = 最终容量的字节数（4096B）

**验证功能**：
- allocs > 1：确认 allocator 在每次扩容时都触发探针
- max_size > min_size：确认不同尺寸的分配都被正确追踪
- frees = allocs - 1（最后一次分配在析构时释放）

---

### 13. string_sso — 字符串 SSO 边界

```bash
./mempoint_demo --mode string_sso --count 100
```

**原理**：GCC `std::string` 有 SSO（Small String Optimization），短字符串（≤15 字节）存储在对象内部，不触发堆分配。超出 SSO 后，`append` 可能触发 reallocation（allocate 新块 + deallocate 旧块）。TrackedAllocator 自动追踪这些分配。

**行为**：
1. `s.append("hi")` → SSO 内，无堆分配
2. 循环 `append("chunk_i ")` → 超出 SSO 后首次 allocate
3. 继续追加 → 多次 reallocate（容量翻倍）

**SDK API**：`tracked::string<MemPointKey::LOG_STRING_SSO>`

**点位**：`Log / String_sso`

**预期特征**（N=100）：
- `allocs ≥ 1`（至少 1 次堆分配，从 SSO 溢出开始）
- `current = 0`（函数结束后 string 析构）
- `max_size` > 15B（至少有一次超出 SSO 的堆分配）
- `min_size` 可能 ≤ 15B（首次 SSO 溢出时的分配大小）

**验证功能**：
- SSO 阶段无堆分配：首次 append("hi") 不产生探针
- 溢出后分配可见：确认 allocator 正确捕获 SSO→heap 的跳变

---

### 14. map_churn — map 大量插入删除

```bash
./mempoint_demo --mode map_churn --count 100
```

**原理**：`tracked::unordered_map` 的每个键值对节点和 bucket 数组都是独立的堆分配。insert 产生节点 allocate，erase 产生节点 deallocate。当 bucket 数量变化时还会产生 bucket 数组的 allocate/deallocate。

**行为**（默认 count=100）：
1. 插入 100 个键值对 → 100 次节点 allocate + 若干 bucket allocate
2. 删除 90 个 → 90 次节点 deallocate
3. 再插入 50 个 → 50 次节点 allocate

**SDK API**：`tracked::unordered_map<int, int, MemPointKey::CONN_MAP_CHURN>`

**点位**：`Connection / Map_churn`

**预期特征**（默认 count=100）：
- `allocs ≈ 100 + 50 + bucket_rehashes ≈ 154`
- `frees ≈ 90 + bucket_rehashes` ≈ 154（函数结束时 map 析构释放剩余节点）
- `current = 0`（函数结束后 map 析构归零）

**验证功能**：
- 节点级追踪：每次 insert/erase 都精确触发探针
- churn 指标：大量 insert/erase 操作的内存分配/释放被正确统计

---

### 15. spike — 内存尖峰

```bash
./mempoint_demo --mode spike
```

**原理**：分配 10 MiB 缓冲区后立即释放（clear + shrink_to_fit）。展示 peak 和 current 的分离——peak 记录历史最大值，current 反映当前实际占用。

**行为**：
1. `resize(10*1024*1024)` → allocate(10 MiB)
2. `clear()` → 不释放内存（vector 保持 capacity）
3. `shrink_to_fit()` → allocate(0) + deallocate(10 MiB)

**SDK API**：`tracked::vector<char, MemPointKey::RPC_SPIKE>`

**点位**：`Rpc / Spike`

**预期特征**：
- `allocs ≈ 1~2`（resize 1 次 + shrink_to_fit 可能 1 次）
- `frees ≈ 1~2`
- `peak ≈ 10 MiB`
- `current = 0`（shrink_to_fit 后完全释放）
- `total ≈ 10 MiB`

**验证功能**：
- peak/current 分离：peak 保留历史最大值，current 归零
- shrink_to_fit 追踪：确认 allocator 正确处理 deallocation

---

### 16. nested — 嵌套容器（双 key）

```bash
./mempoint_demo --mode nested --count 100
```

**原理**：`tracked::vector<tracked::string<K2>, K1>` 使用两个不同的 key。vector 的元素数组分配走 K1，每个 string 的堆分配走 K2。两条追踪通道完全独立，在 `spdiag mempoint` 输出中显示为两行。

**行为**：
1. `reserve(100)` → K1: allocate(100 × sizeof(string) ≈ 3200B)
2. 循环 `push_back(string(100, 'x'))`:
   - 每次 string 构造 → K2: allocate(100B)（超出 SSO）
   - vector 可能扩容 → K1: allocate + deallocate

**SDK API**：`tracked::vector<tracked::string<MemPointKey::SQL_NESTED_STR>, MemPointKey::SQL_NESTED_VEC>`

**点位**：`Sql / Nested_vec`（vector 层）和 `Sql / Nested_str`（string 层）

**预期特征**（N=100）：
- Nested_vec 行：`allocs ≈ 1~8`（vector 的几次扩容），`current = 0`
- Nested_str 行：`allocs = 100`（每个 string 1 次堆分配），`current = 0`

**验证功能**：
- 双 key 独立：同一容器嵌套中两个 key 的数据完全独立统计
- 层次追踪：vector 层和 string 层的分配分别可观测

---

### 17. realloc_cascade — 连续扩容缩容

```bash
./mempoint_demo --mode realloc_cascade
```

**原理**：对 vector 执行一系列 resize/shrink_to_fit 操作，展示 allocator 在每次堆分配变化时的精确追踪。vector 的 `resize` 到更大容量会 allocate，`shrink_to_fit` 会 deallocate + allocate 精确匹配的块。注意 realloc 期间新旧两块短暂共存，`peak` 反映瞬间的真实内存占用。

**行为**：
1. `resize(100)` → allocate(400B)
2. `resize(1000)` → allocate(4000B) + deallocate(400B)
3. `resize(500)` → 无分配（vector 不缩容）
4. `shrink_to_fit()` → allocate(2000B) + deallocate(4000B)
5. `resize(2000)` → allocate(8000B) + deallocate(2000B)
6. `clear() + shrink_to_fit()` → deallocate(8000B)（空 vector 的 shrink_to_fit 只释放不分配）

**SDK API**：`tracked::vector<int, MemPointKey::SQL_REALLOC>`

**点位**：`Sql / Realloc_cascade`

**预期特征**：
- `allocs = 4`（步骤 1,2,4,5 的 allocate；步骤 6 空 vector shrink_to_fit 只释放不分配）
- `frees = 4`（步骤 2,4,5 的 realloc deallocate + 步骤 6 的最终释放）
- `current = 0`（最终完全释放）
- `max_size = 8000B`（步骤 5 的分配）
- `min_size = 400B`（步骤 1 的分配）
- `peak ≈ 10000B`（步骤 5 realloc 瞬间新旧块叠加：8000 + 2000 = 10000）

**验证功能**：
- resize 不缩容：步骤 3 不产生探针，确认 allocator 层追踪的是真实堆分配
- shrink_to_fit 追踪：步骤 4 正确记录 allocate+deallocate 对
- 多尺寸追踪：max_size 和 min_size 正确反映多次不同尺寸的分配
- peak 精确性：realloc 期间新旧块短暂共存，peak 捕获瞬间的真实内存占用

---

## 模式对比总结

| 模式 | Category | Key | allocs vs frees | current | 泄漏 |
|------|----------|-----|----------------|---------|------|
| stack_value | Rpc | Stack_value | allocs == frees | 0 | 无 |
| tracked_vector | Sql | Tracked_vector | allocs == 1 | 0 | 无 |
| tracked_string | Log | Tracked_string | allocs == 1 | 0 | 无 |
| tracked_hashmap | Connection | Tracked_hashmap | allocs == 1 | 0 | 无 |
| raw_buffer | Rpc | Raw_buffer | allocs == frees | 0 | 无 |
| leak | Transaction | Leak | allocs > frees | 持续增长 | 有 |
| struct_member | Sql | Struct_member | allocs == frees | 锯齿形 | 无 |
| heap_object | Execution | Heap_object | allocs == N | 批量后归零 | 无 |
| object_pool | Net | Object_pool | allocs == frees | 稳定 | 无 |
| custom_lifecycle | Async | Custom_lifecycle | allocs ≈ frees | 先增后稳定 | 无 |
| vector_growth | Sql | Vector_growth | allocs ≈ log₂N | 0 | 无 |
| string_sso | Log | String_sso | allocs ≥ 1 | 0 | 无 |
| map_churn | Connection | Map_churn | allocs >> frees | > 0 | 无 |
| spike | Rpc | Spike | allocs ≈ frees | 0 | 无 |
| nested | Sql | Nested_vec + Nested_str | 双 key 独立 | 0 | 无 |
| realloc_cascade | Sql | Realloc_cascade | allocs ≈ frees | 0 | 无 |
| benchmark | Rpc | Stack_value | — | — | — |

## 典型使用流程

### 验证 MemPoint 基本功能

```bash
./mempoint_demo --wait
sudo spdiag mempoint --pid <PID>
# 按 Enter 放行，观察各 Category/Key 行的输出
```

### 持续循环观测

```bash
./mempoint_demo --mode heap_object --count 50 --loop --interval 200
sudo spdiag mempoint --pid <PID>
# 观察 Execution/Heap_object 行的锯齿形变化
```

### 诊断内存泄漏

```bash
./mempoint_demo --mode leak --count 100 --loop --wait
sudo spdiag mempoint --pid <PID> --show leaked,current,allocs,frees
# Transaction/Leak 行 Frees=0, Current 持续增长
```

### 测量生产环境开销

```bash
# 无 eBPF 附加
./mempoint_demo --mode benchmark --iterations 1000000
# 有 eBPF 附加
sudo spdiag mempoint --pid <PID>
./mempoint_demo --mode benchmark --iterations 1000000
```

## 常见问题

| 现象 | 可能原因 | 解决方法 |
|------|---------|---------|
| "requires SPDIAG_ENABLE_MEMPOINT" | 编译时未启用 MemPoint | `bash build.sh -m on` |
| mempoint 无数据输出 | USDT 探针未激活或 eBPF 未附加 | 确认目标进程运行中，以 root 运行 spdiag |
| leak 模式无 Leak Detection 区域 | demo 尚未执行到泄漏代码 | 使用 `--wait` 模式 |
| 所有点位 current = 0 | demo 已运行完毕 | 使用 `--loop` 或 `--wait` 保持活跃 |
