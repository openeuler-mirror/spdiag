# spdiag 缓存命名与标准 perf 工具对照

## 1. 缓存级别命名对照表

| 序号 | 旧命名 (spdiag) | 新命名 (perf 标准) | 含义 |
|---|---|---|---|
| 1 | `L1D` | `L1-dcache` | L1 数据缓存 |
| 2 | `L1I` | `L1-icache` | L1 指令缓存 |
| 3 | `LL` | `LLC` | 末级缓存 (Last Level Cache) |
| 4 | `DTLB` | `dTLB` | 数据 TLB |
| 5 | `ITLB` | `iTLB` | 指令 TLB |
| 6 | `BPU` | `branch` | 分支预测单元 |

## 2. PERF_COUNT_HW_CACHE 常量映射（未修改）

| 新命名 | 内核常量 |
|---|---|
| `L1-dcache` | `PERF_COUNT_HW_CACHE_L1D` |
| `L1-icache` | `PERF_COUNT_HW_CACHE_L1I` |
| `LLC` | `PERF_COUNT_HW_CACHE_LL` |
| `dTLB` | `PERF_COUNT_HW_CACHE_DTLB` |
| `iTLB` | `PERF_COUNT_HW_CACHE_ITLB` |
| `branch` | `PERF_COUNT_HW_CACHE_BPU` |

## 3. ARM PMU 事件对照（未修改）

| 新命名 | ARM PMU 事件 (ref) | ARM PMU 事件 (miss) |
|---|---|---|
| `L1-dcache` | `L1D_CACHE` | `L1D_CACHE_REFILL` |
| `L1-icache` | `L1I_CACHE` | `L1I_CACHE_REFILL` |
| `LLC` | `LL_CACHE_RD` | `LL_CACHE_MISS_RD` |
| `dTLB` | `L1D_TLB` | `L1D_TLB_REFILL` |
| `iTLB` | `L1I_TLB` | `L1I_TLB_REFILL` |
| `branch` | `BR_PRED` | `BR_MIS_PRED` |

## 4. 差异分析

| 对比维度 | spdiag (旧) | perf 标准 (新) | 差异说明 |
|---|---|---|---|
| L1 数据缓存 | `L1D` | `L1-dcache` | 短缩写 → 描述性带连字符 |
| L1 指令缓存 | `L1I` | `L1-icache` | 短缩写 → 描述性带连字符 |
| 末级缓存 | `LL` | `LLC` | 缺少 C (Cache) |
| 数据 TLB | `DTLB` | `dTLB` | 全大写 → 小写 d + 大写 TLB |
| 指令 TLB | `ITLB` | `iTLB` | 全大写 → 小写 i + 大写 TLB |
| 分支预测 | `BPU` | `branch` | 硬件术语缩写 → 通用词 |

## 5. 用户使用变更

```bash
# 旧写法
spdiag cachestat --pid 1234 --cache-levels L1D,L1I,LL,DTLB

# 新写法
spdiag cachestat --pid 1234 --cache-levels L1-dcache,L1-icache,LLC,dTLB
```

## 6. 涉及文件清单

| 分类 | 文件 |
|---|---|
| 核心源码 | `src/runtime/plugins/ob_point/cache_sampler.cpp` |
| | `src/cli/cli_config.cpp` |
| | `src/cli/cli_config.h` |
| | `src/cli/cache_display.cpp` |
| | `src/runtime/plugins/cachestat/cachestat.h` |
| 测试 | `tests/test_cli_config.cpp` |
| | `tests/test_display_engine.cpp` |
| | `tests/test_display_engine_cache.cpp` |
| 文档 | `docs/zh/DESIGN.md` |
| | `docs/zh/CLI_REFERENCE.md` |
| | `docs/zh/OVERVIEW.md` |
| | `QuickStart.md` |
| | `CHANGELOG.md` |
| | `README.md` |
| | `examples/CACHESTAT_DEMO_README.md` |

## 7. commit 信息

```
refactor(cache): 缓存级别命名对齐标准 perf 工具命名，L1D->L1-dcache / L1I->L1-icache / LL->LLC / DTLB->dTLB / ITLB->iTLB / BPU->branch，涉及 src/tests/docs 共 15 个文件
```
