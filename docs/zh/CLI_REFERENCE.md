# SpDiag CLI 命令参考

## 概述

```
spdiag <command> [options]
```

完整功能构建提供 11 个 CLI 命令，分为三类。`memstat`、`cachestat`、`mempoint` 受编译选项控制，功能关闭时不会出现在命令表中：

| 类别 | 命令 | 说明 |
|------|------|------|
| 共享内存管理 | start, stop, restart, status, clear | 管理共享内存的创建、销毁和数据清理 |
| 性能统计查看 | show, watch, history | 读取和展示性能采集数据 |
| Linux 观测 | memstat, cachestat, mempoint | memstat/mempoint 使用 eBPF，cachestat 使用 perf_event |

## 全局选项

| 选项 | 说明 |
|------|------|
| `-h, --help` | 显示帮助信息 |
| `--version` | 显示版本号 |
| `--config <path>` | 当前禁用；配置仍会从默认路径自动加载 |
| `--csv [dir]` | 为有表格输出的命令生成 CSV；省略目录时使用 `./results` |

> **CSV 落盘说明**：`--csv [dir]` 可用于 `show`、`watch`、`history` 和三个观测命令，不能用于共享内存管理命令。详见 [CSV 输出](#七csv-落盘输出)。

---

## 一、共享内存管理命令

### start

创建共享内存。

```bash
spdiag start
```

### stop

销毁共享内存。

```bash
spdiag stop
```

### restart

重建共享内存（先 stop 再 start）。

```bash
spdiag restart
```

### status

查看共享内存状态。

```bash
spdiag status
```

### clear

清空共享内存中的性能数据。需要共享内存已存在。

```bash
spdiag clear
```

---

## 二、性能统计查看命令

### show

显示当前性能统计快照。需要共享内存已存在（先 `spdiag start`）。

```bash
spdiag show [options]
```

#### 示例

```bash
spdiag show                          # 默认汇总视图
spdiag show -d                       # 显示每核详情
spdiag show -c 0-3                   # 只看 core 0~3
spdiag show -l ">=2"                 # 只看 level >= 2
spdiag show -l 2-3                   # level 范围 2~3
spdiag show -s ticks:desc             # 按 ticks 降序排列
spdiag show --strict-snapshot        # 严格快照一致性检查
spdiag show --csv ./output          # 输出表格 + 落盘 CSV 到 ./output
```

### watch

持续监控性能统计。按 `Ctrl+C` 退出。

```bash
spdiag watch [options]
```

与 `show` 共享相同的过滤和排序选项，额外支持：

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--interval <ms>` | 2000 | 刷新间隔（毫秒） |

#### 示例

```bash
spdiag watch                         # 每 2 秒刷新一次
spdiag watch --interval 500 -d       # 每 500ms 刷新，显示每核详情
spdiag watch -l ">2" -s ticks:desc # level > 2，按 ticks 降序
spdiag watch --csv ./output          # 持续监控 + 落盘 CSV（每个视图独立文件）
```

### history

查看历史转储记录。

```bash
spdiag history [options]
```

| 选项 | 说明 |
|------|------|
| `-n <N>` | 显示最近 N 条记录 |
| `-i <N>` | 显示第 N 条记录的详情（0 = 最新） |
| `-H, --clear-history` | 清除历史记录 |

#### 示例

```bash
spdiag history                       # 显示所有历史记录
spdiag history -n 5                  # 显示最近 5 条
spdiag history -i 0                  # 显示最新一条的详情
spdiag history --csv ./output        # 历史 + 落盘 CSV
```

### show / watch / history 公共选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--history-size <N>` | 30 | 历史环形缓冲区容量 |
| `--data-dir <path>` | `/tmp/spdiag_data` | 持久化数据目录 |

---

## 三、过滤与排序选项（show / watch / memstat / mempoint）

### -d, --detail

显示每核详情（show / watch）或每条记录的调用栈树（memstat）。

### -c, --core N[-M]

按 CPU 核号过滤（仅 show / watch）。

```bash
-c 0          # 只看 core 0
-c 0-3        # 看 core 0~3
```

核号必须 >= 0，范围起始 <= 结束。

### -l, --level EXPR

按性能级别过滤（仅 show / watch，1~4）。支持运算符和范围：

| 写法 | 含义 |
|------|------|
| `-l 2` | level <= 2（默认运算符） |
| `-l ">2"` | level > 2（shell 中需要引号） |
| `-l ">=2"` | level >= 2 |
| `-l "=3"` | level == 3 |
| `-l "<3"` | level < 3 |
| `-l 2-3` | level 在 2~3 范围内 |

### -s, --sort KEY[:DIR][,...]

多键排序，逗号分隔多个字段，左优先。排序方向通过冒号指定。

```bash
-s ticks:desc          # ticks 降序
-s name:asc            # name 升序
-s total:desc,name:asc # 先按 total 降序，再按 name 升序
```

PerfPoint 可用字段（show / watch）：

| 字段 | 说明 | 默认排序 |
|------|------|---------|
| `name` | 性能点名称 | 升序 |
| `lvl` | 级别 | 升序 |
| `ticks` | 采集次数 | 降序 |
| `good` | 正常次数 | 降序 |
| `bad` | 异常次数 | 降序 |
| `total` | 总耗时 | 降序 |
| `avg` | 平均耗时 | 降序 |
| `min` | 最小耗时 | 降序 |
| `max` | 最大耗时 | 降序 |

Memstat / MemPoint 共享字段：

| 字段 | 说明 | 默认排序 |
|------|------|---------|
| `current` | 当前内存 | 降序 |
| `peak` | 峰值内存 | 降序 |
| `total` | 累计分配 | 降序 |
| `allocs` | 分配次数 | 降序 |
| `frees` | 释放次数 | 降序 |
| `max_size` | 最大分配 | 降序 |
| `min_size` | 最小分配 | 降序 |
| `first_seen` | 首次出现 | 升序 |
| `leaked` | 泄漏量 | 降序 |
| `avg_size` | 平均分配 | 降序 |
| `frame` | 调用栈帧 | 升序 |

### --filter PAT[,PAT]...

Glob 模式过滤（支持 `*` 和 `?`）。保留匹配任一模式的条目。

- PerfPoint：匹配 `Module.Point` 格式
- Memstat：匹配调用栈帧
- MemPoint：区分大小写地匹配 `category.keyName`（如 `Sql.*`、`Rpc.*`）

```bash
--filter "Compute*,Memory*"    # 匹配以 Compute 或 Memory 开头
--filter "*Cache*,*alloc*"     # 匹配包含 Cache 或 alloc
```

### --strict-snapshot

快照一致性检查失败时报错退出（仅 show / watch）。默认情况下会静默跳过不一致的数据。

---

## 四、Linux 观测命令

memstat 和 mempoint 通过 eBPF 实现进程级/点位级监控，cachestat 使用 `perf_event_open` 读取 PMU。memstat 和 cachestat 无需修改目标程序，mempoint 需要目标程序集成 SDK。这三个命令只在对应编译功能启用时可用。

### memstat

监控进程的内存分配情况。

```bash
spdiag memstat --pid <PID> [options]
```

按 `Ctrl+C` 退出。

#### 必需选项

| 选项 | 说明 |
|------|------|
| `--pid <PID>` | 目标进程 PID |

#### 可选选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--interval <ms>` | 1000 | 采集间隔（毫秒） |
| `--libc-path <path>` | `/usr/lib64/libc.so.6` | 指定被跟踪 libc 路径 |
| `--bytes` | — | 显示原始字节数（默认人类可读格式） |
| `--no-filter` | 关闭 | 显示原始栈帧（不过滤） |
| `--detail` | 关闭 | 显示每条记录的调用栈树 |
| `--sort KEY[:DIR][,...]` | `current,peak,allocs` | 排序字段和方向（见 Section 3） |
| `--filter PAT[,PAT]...` | — | Glob 模式过滤调用栈帧（见 Section 3） |
| `--group-by <mode>` | `tid+frame` | 聚合模式，可选：`tid+frame`、`frame`、`raw` |
| `--top <N>` | 20 | 显示前 N 组 + [others] 汇总行 |
| `--show <fields>` | — | 用指定列替换默认指标列；PID、TID、CallStack 始终显示。可选：`current, total, allocs, frees, peak, min_size, max_size, first_seen, leaked, avg_size, churn, churn_rate, alloc_rate` |

#### 示例

```bash
spdiag memstat --pid 1234
spdiag memstat --pid 1234 --interval 500
spdiag memstat --pid 1234 --group-by frame --top 10
spdiag memstat --pid 1234 --bytes --show min_size,leaked
spdiag memstat --pid 1234 --detail --no-filter
spdiag memstat --pid 1234 --sort leaked:desc --top 5
spdiag memstat --pid 1234 --filter "*malloc*,*calloc*"
```

### cachestat

监控进程的缓存命中率。

```bash
spdiag cachestat --pid <PID> [options]
```

#### 必需选项

| 选项 | 说明 |
|------|------|
| `--pid <PID>` | 目标进程 PID |

#### 可选选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--interval <ms>` | 1000 | 采集间隔（毫秒） |
| `--libc-path <path>` | `/usr/lib64/libc.so.6` | libc.so 路径 |
| `--cache-levels <list>` | `L1-dcache,L1-icache,LLC` | 监控的缓存级别，逗号分隔，可选：`L1-dcache`、`L1-icache`、`LLC`、`dTLB`、`iTLB`、`branch` |
| `--delta` | off | 显示每秒增量而非累计值 |

#### 示例

```bash
spdiag cachestat --pid 1234
spdiag cachestat --pid 1234 --cache-levels L1-dcache,LLC,dTLB
spdiag cachestat --pid 1234 --interval 500
spdiag cachestat --pid 1234 --delta
```

### mempoint

监控进程中 MemPoint SDK 标记的点位级内存使用。需要目标程序集成 MemPoint SDK。

```bash
spdiag mempoint --pid <PID> [options]
```

按 `Ctrl+C` 退出。

#### 必需选项

| 选项 | 说明 |
|------|------|
| `--pid <PID>` | 目标进程 PID |

#### 可选选项

| 选项 | 默认值 | 说明 |
|------|--------|------|
| `--interval <ms>` | 1000 | 采集间隔（毫秒） |
| `--bytes` | — | 显示原始字节数（默认人类可读格式） |
| `--sort KEY[:DIR][,...]` | `current,peak` | 排序字段和方向（键与 memstat 共享） |
| `--filter PAT[,PAT]...` | — | Glob 模式过滤，匹配 `category.keyName` |
| `--top <N>` | 20 | 显示前 N 条 |
| `--show <fields>` | — | 自定义显示列（Category/Key 始终显示）。可选：`current,total,allocs,frees,peak,min_size,max_size,first_seen,leaked,avg_size,churn,churn_rate,alloc_rate` |

ObjectMemPoint 的字段级快照使用独立的 `fields` 输出区域；CLI 的 `--show` 只接受上表列名，`--show fields` 会被参数校验拒绝。

#### 示例

```bash
spdiag mempoint --pid 1234
spdiag mempoint --pid 1234 --sort peak --top 5
spdiag mempoint --pid 1234 --filter "Sql.*" --bytes
spdiag mempoint --pid 1234 --show leaked,churn_rate
spdiag mempoint --pid 1234 --interval 500 --sort allocs:desc
```

#### 默认输出列

| 列 | 说明 |
|----|------|
| `#` | 序号 |
| `Category` | 点位分类（rpc、sql、sess、tx、log） |
| `Key` | 点位描述名称 |
| `Current` | 当前内存使用 |
| `Total` | 累计分配总量 |
| `Allocs` | 分配次数 |
| `Frees` | 释放次数 |
| `Peak` | 峰值内存 |
| `AvgSize` | 平均分配大小 |
| `MaxSize` | 最大单次分配 |
| `FirstSeen` | 首次出现时间 |

当检测到泄漏时，额外显示 Leak Detection 区域。

---

## 五、性能级别体系

| 级别 | 名称 | 说明 |
|------|------|------|
| 1 | SubSystem | 关键路径，必须监控 |
| 2 | KeyModule | 重要模块 |
| 3 | Module | 普通模块 |
| 4 | Debug | 调试信息 |

---

## 六、配置文件

默认搜索路径（按顺序）：

1. `config/spdiag.conf`
2. `spdiag.conf`
3. `/etc/spdiag/spdiag.conf`

格式为 INI 风格：

```ini
[shm]
name = /spdiag_shm_default

[general]
version = 1
data_dir = /tmp/spdiag_data
history_size = 30
dump_interval_ms = 0
level_gate = 0
num_cores = 0

[perf]
maxSlots = 192
maxPrograms = 1
```

上例与仓库的 `config/spdiag.conf.example` 一致。若没有配置文件，CLI 结构体默认值为 `maxSlots=1024`、`maxPrograms=8`；SDK 本地分片的编译期上限 `MAX_PERF_SLOTS` 当前为 256，三者含义不要混淆。`--config` 和 `--shm` 当前处于禁用状态，不能覆盖默认搜索结果。

---

## 七、CSV 落盘输出

`--csv [dir]` 用于 `show`、`watch`、`history`、`memstat`、`cachestat`、`mempoint`。不带目录时写入 `./results`；共享内存管理命令会拒绝该选项。

### 用法

```bash
spdiag show --csv /path/to/output
spdiag show --csv                  # 使用 ./results
spdiag watch --csv ./reports
spdiag history --csv /tmp/csv_data
spdiag memstat --pid 1234 --csv ./data
spdiag cachestat --pid 1234 --csv ./data
spdiag mempoint --pid 1234 --csv ./data
```

### CSV 文件命名规则

每个视图生成独立的 CSV 文件，文件名格式：`{前缀}_{YYYYMMDD_HHmmss}.csv`

| 命令 / 视图 | CSV 文件前缀 |
|-----------|-------------|
| `show` — 汇总视图 | `spdiag_perfpoint_summary` |
| `show --detail` — 详情视图 | `spdiag_perfpoint_detail` |
| `show` — 原始表视图 | `spdiag_rawtable` |
| `history` | `spdiag_history` |
| `history -i <N>` | `spdiag_history_detail` |
| `memstat` | `spdiag_memstat` |
| `cachestat` | `spdiag_cachestat` |
| `mempoint` | `spdiag_mempoint` |
| `show --perflog` | `spdiag_perflog` |

### 注意事项

- 不传 `--csv` 则不生成任何 CSV 文件
- 不传 `--csv` 时不会创建 CSV 输出会话；传入后在展示阶段同步写文件，不改变采集、排序和过滤逻辑
- CSV 文件为标准格式，逗号分隔，含引号转义，可直接用 Excel / WPS / Google Sheets 打开
- 目录不存在时会自动创建
- 文件名时间戳精度为秒，写入方式为 truncate；同一目录、同一前缀在同一秒内再次启动可能覆盖已有文件

---

## 八、错误诊断

### 常见错误信息

| 错误信息 | 原因 | 解决方法 |
|----------|------|----------|
| `spdiag is not running. Please run 'spdiag start' first.` | 共享内存未创建 | 先执行 `spdiag start` |
| `Multiple commands specified` | 命令行包含多个命令 | 只指定一个命令 |
| `Option not applicable to 'xxx' command` | 参数与命令不匹配 | 参考上方各命令的可用参数 |
| `Unknown argument: xxx` | 无法识别的参数 | 检查拼写，运行 `spdiag --help` |
| `required for memstat/cachestat/mempoint` | 观测命令未指定 `--pid` | 添加 `--pid <PID>` |
| `missing value` | 参数后缺少值 | 补上参数值 |
| `range start must be <= end` | `--core` 范围起始大于结束 | 交换顺序，如 `-c 0-3` |

### 未指定命令时的行为

如果不指定命令直接传入选项参数（如 `spdiag -d -c 0-3`），默认使用 `show` 命令，并打印警告：

```
Warning: no command specified, defaulting to 'show'.
Run 'spdiag --help' for available commands.
```

---

## 八、典型使用流程

### PerfPoint 性能采集

```bash
# 1. 启动共享内存
spdiag start

# 2. 启动目标程序（已集成 PerfPoint SDK）
./your_program

# 3. 查看性能数据
spdiag show                    # 汇总视图
spdiag show -d -l ">=2"        # 详情视图，只看重要级别
spdiag watch --interval 1000   # 持续监控

# 4. 停止采集
spdiag stop
```

### PerfLog 探针调用日志

PerfLog 需先集成 PerfPoint SDK。编译时加 `-s on`，启动时加 `--perflog`：

```bash
# 1. 编译（启用 PerfLog）
bash build.sh -s on
# 或手动 cmake：cmake .. -DENABLE_PERFLOG=ON

# 2. 启动共享内存 + PerfLog
sudo spdiag start --perflog

# 3. 启动目标程序（与 PerfPoint 完全相同）
./your_program

# 4. 查看探针调用日志
spdiag show --perflog           # 一次性输出
spdiag watch --perflog          # 持续刷新（Ctrl+C 退出）

# 5. 停止采集
spdiag stop
```

输出格式（`|` 分隔，左对齐）：

```
Datetime                         | PID   | TID     | CPU   | Level | Program        | Module.Point                  | Cost(ns)
-------------------------------------------------------------------------------------------------------------------------
2026-06-10 14:32:01.123456789    | 12345 | 12345   | 3     | 1     | my_app         | Compute.MatMul                | 150
```

| 列 | 含义 |
|---|---|
| Datetime | 挂钟时间（纳秒精度，YYYY-MM-DD HH:MM:SS.nnnnnnnnn） |
| PID | 进程 ID |
| TID | 线程 ID |
| CPU | CPU 核号 |
| Level | 性能级别（1=SubSystem, 2=KeyModule, 3=Module, 4=Debug） |
| Program | 程序名称 |
| Module.Point | 模块.探针名 |
| Cost(ns) | 单次耗时（纳秒） |

每个探针独立保留最近 100 条记录，按挂钟时间升序排列。如果仅用 `spdiag start`（不加 `--perflog`），则仅采集 PerfPoint 统计信息，不记录调用日志。

### eBPF 内存观测（无需修改目标程序）

```bash
# 直接观测任意进程
spdiag memstat --pid $(pidof your_program)
spdiag cachestat --pid $(pidof your_program) --cache-levels L1-dcache,LLC
```

### MemPoint 点位级内存观测（需集成 SDK）

```bash
# 观测已集成 MemPoint SDK 的进程
spdiag mempoint --pid $(pidof your_program)
spdiag mempoint --pid $(pidof your_program) --sort peak --top 5
spdiag mempoint --pid $(pidof your_program) --filter "Sql.*" --show leaked,churn_rate
```

### 目标程序编译建议（完整调用栈）

```makefile
CFLAGS   += -g -fno-omit-frame-pointer -fasynchronous-unwind-tables
CXXFLAGS += -g -fno-omit-frame-pointer -fasynchronous-unwind-tables
LDFLAGS  += -Wl,--build-id=sha1
# 不要 strip .symtab / .debug_line / .debug_info 段
```

---

## 九、测试 CLI 命令

### 方法一：单元测试

CLI 参数解析和校验逻辑通过 Google Test 覆盖，测试文件位于 `tests/test_cli_config.cpp`。

测试覆盖范围：

| 测试类别 | 示例用例 | 验证内容 |
|----------|---------|---------|
| 默认值 | `CliConfigTest.DefaultValues` | 所有字段的默认值正确 |
| 命令解析 | `ParseShow` / `ParseMemstat` / `ParseCachestat` | 命令名正确映射到枚举 |
| 参数解析 | `ParseCoreRange` / `ParseLevelFilter` / `ParseSortField` | 各选项值正确写入配置 |
| 跨命令校验 | `ShowWithPidOption` / `StartWithShowOptions` | 不属于该命令的选项报错 |
| 参数值校验 | `GroupByInvalidValue` / `CacheLevelsInvalid` | 非法参数值报错 |
| 未知参数 | `UnknownArgument` | 无法识别的参数报错 |

运行方式：

```bash
# 编译
bash build.sh

# 运行全部测试
cd build && ctest --output-on-failure

# 只运行 CLI 配置相关测试
ctest --test-dir build -R "CliConfig" --output-on-failure
```

添加新的 CLI 参数时，应同步在 `test_cli_config.cpp` 中补充：
- 正常解析测试（`CliConfigTest` 组）
- 非法值 / 缺失值测试（`CliConfigErrorTest` 组）
- 跨命令校验测试（确保参数仅在允许的命令下生效）

### 方法二：人工测试

通过直接运行编译后的二进制进行端到端验证。

```bash
# 查看帮助信息，确认所有选项正确列出
./build/spdiag --help
./build/spdiag --version

# 测试参数校验（应报错）
./build/spdiag start -d                    # "Option not applicable"
./build/spdiag show --pid 1234             # "Option not applicable"
./build/spdiag memstat                     # "required for memstat"
./build/spdiag show --foobar               # "Unknown argument"

# 测试正常流程（需要共享内存）
./build/spdiag start
./build/spdiag show
./build/spdiag show -d -c 0-3 -l ">=2"
./build/spdiag stop

# 测试 eBPF 观测（需要 root 权限，仅 Linux）
sudo ./build/spdiag memstat --pid <PID> --bytes --detail
sudo ./build/spdiag cachestat --pid <PID> --cache-levels L1-dcache,LLC
```
