# 单元测试配置与运行指南

## 快速开始

`build.sh` 统一生成 RPM，并在打包时自动运行测试。本地开发使用 CMake，不安装到系统：

```bash
# 构建时启用测试
cmake -S . -B build -DBUILD_TESTS=ON -DENABLE_MEMPOINT=OFF
cmake --build build --parallel

# 运行所有测试
ctest --test-dir build --output-on-failure

# 运行单个测试
./build/tests/test_aggregator
./build/tests/test_perf_point

# 用 ctest 过滤
ctest --test-dir build -R '^test_aggregator$' --output-on-failure
ctest --test-dir build -R '^test_perf_point$' --output-on-failure
ctest --test-dir build -V
```

## 离线环境

如果无法从 GitHub 自动下载 GTest，指定本地 googletest 源码：

```bash
SPDIAG_GTEST_SOURCE=/path/to/googletest \
  cmake -S . -B build -DBUILD_TESTS=ON -DENABLE_MEMPOINT=OFF
cmake --build build --parallel
```

或设置环境变量：

```bash
export SPDIAG_GTEST_SOURCE=/path/to/googletest
cmake -S . -B build -DBUILD_TESTS=ON -DENABLE_MEMPOINT=OFF
cmake --build build --parallel
```

## GTest 安装策略

CMake 按以下优先级检测 GTest：

1. **环境变量** `SPDIAG_GTEST_SOURCE` — 指定本地 googletest 源码（离线首选）
2. **系统安装** — `find_package(GTest)` 查找系统包管理器安装的 GTest
3. **FetchContent 自动下载** — 从 GitHub 自动下载 googletest v1.14.0

系统安装 GTest（可选）：

```bash
# openEuler / RHEL / Fedora
sudo dnf install gtest gtest-devel

# Debian / Ubuntu
sudo apt-get install libgtest-dev
```

## `user_def_test` 与安装依赖

`tests/user_def_test` 验证的是**已经安装**的 SDK、CMake package 和用户自定义 PerfPoint `.def`。先通过 RPM 安装 SDK，再手动构建该独立项目。

当前顶层 CTest 只有在配置阶段发现 `tests/user_def_test/build/user_def_test` 已存在时才注册该测试。这样普通 CMake 构建和 RPM `%check` 不会因为一个尚未安装、尚未构建的外部验证程序失败。

```bash
# 只构建和运行仓库内自包含测试
cmake -S . -B build -DBUILD_TESTS=ON -DENABLE_MEMPOINT=OFF
cmake --build build --parallel
ctest --test-dir build --output-on-failure

# 安装后手动验证用户项目接入
cmake -S tests/user_def_test -B tests/user_def_test/build
cmake --build tests/user_def_test/build
./tests/user_def_test/build/user_def_test

# 验证 SPDIAG_DISABLE 保持原调用语法且产物中不残留 SDK 符号
bash tests/user_def_test/build_and_run.sh disabled
```

如果该二进制是在顶层 CMake 配置完成后才生成的，需要重新运行一次 CMake，CTest 才会把它加入测试列表。

## RPM 构建中的测试

当前 `spdiag.spec` 使用 `BUILD_TESTS=ON` 构建共享版本，并在 `%check` 执行：

```bash
ctest --test-dir build --output-on-failure
```

因此 `bash build.sh`（兼容别名 `bash build.sh package`）也会运行仓库内自包含测试；任一测试失败都会终止 RPM 构建。spec 声明 `gtest-devel` 和 `kernel-headers` 为构建依赖。随后单独配置 `build-static`，只构建静态 SDK，不重复执行测试。

成功后终端打印独立的 `output/<构建ID>/` 和完整安装命令。RPM 工作目录每次独立创建并自动清理；需要手动运行测试二进制时，使用上文的本地 CMake 构建。

`test_build_rpm` 已注册到 CTest，RPM 的 `%check` 也会运行。它不需要 aarch64 或真实 `rpmbuild`，使用 Git 归档和模拟打包命令验证来源 SHA、无 Git 源码重打包、并发隔离、失败不发布，以及六条安装/重装命令的路径和包集合；不会安装系统文件。也可以单独运行：

```bash
bash tests/test_build_rpm.sh
```

`test_install_consumer` 也已注册到 CTest：先用 `DESTDIR` 安装到测试私有目录，再仅从该目录 `find_package(SpDiag)`，编译并链接包含 `spdiag/perf.h` 的消费者，覆盖 `perf_point.def` 等公开文件及 SDK 导出依赖。共享构建验证共享 target，静态构建验证静态 target。

RPM `%check` 还会针对实际 `%{buildroot}` 中动态和静态 SDK 的共同安装，分别编译、链接两个消费者。以上检查不执行消费者程序，交叉编译环境也能验证安装内容和链接闭包；硬件运行验证仍需目标平台。

## 测试文件清单

### SDK 层

| 文件 | 测试目标 |
|------|---------|
| test_perf_point.cpp | PerfPoint 打点 |
| test_perf_point_disabled.cpp | `SPDIAG_DISABLE` 编译期禁用；保持 PerfPoint 调用语法且无需链接 SDK 实现 |
| test_perf_manager.cpp | PerfManager 管理 |
| test_perf_manager_def.cpp | PerfManager + 用户自定义 .def |
| test_perf_shard.cpp | PerfShard 分片 |
| test_perf_keys.cpp | PerfKey 枚举打点 |
| test_perf_key_defined.cpp | PerfKeyDefined 枚举定义 |
| test_perf_timer.cpp | PerfTimer 后台 dump |
| test_perflog.cpp | PerfLog 探针日志 |
| test_global_perf_point.cpp | 全局 PerfPoint（跨线程模式） |
| test_shm_layout.cpp | 共享内存数据结构 |
| test_shm_path.cpp | SHM namespace 规范化、环境变量优先级和数据目录隔离 |
| test_mem_point.cpp | MemPoint 内存追踪核心（需 `SPDIAG_ENABLE_MEMPOINT`） |
| test_object_mem_point.cpp | MemPoint 字段级内存拆解（ObjectMemPoint + FieldSizeTraits，需 `SPDIAG_ENABLE_MEMPOINT`） |
| test_mem_point_sdk.cpp | MemPoint SDK（TrackValue / TrackedAllocator / tracked 容器，需 `SPDIAG_ENABLE_MEMPOINT`） |
| test_platform.cpp | 平台抽象层（GetCurrentCpuId、SHM 权限与 backing reservation 等） |

### Manager 层

| 文件 | 测试目标 |
|------|---------|
| test_aggregator.cpp | 数据聚合 |
| test_dump_recorder.cpp | DumpRecorder |
| test_persistent_aggr.cpp | 持久化聚合 |
| test_multi_shm_namespace.cpp | 双 namespace 生命周期、外部 SHM 防误删、显式恢复和晚启动重连 |
| test_percentile_calculator.cpp | 百分位数计算（需 `SPDIAG_ENABLE_PERCENTILE`） |

### Runtime 层

| 文件 | 测试目标 |
|------|---------|
| test_processor.cpp | Processor |
| test_shm_reader.cpp | ShmReader 跨进程读取 |
| test_plugin_manager.cpp | 插件管理 |
| test_plugin_interface.cpp | 插件接口 |
| test_symbol_resolver.cpp | 符号解析 |
| test_ob_bpf_context.cpp | OB BPF 上下文 |
| test_custom_perfpoint.cpp | CustomPerfpoint 插件 |
| test_memstat_plugin.cpp | Memstat 插件 |
| test_cachestat_plugin.cpp | Cachestat 插件 |
| test_mempoint_plugin.cpp | MemPoint 插件 |
| test_point_mem_profiler.cpp | PointMemProfiler（需 `SPDIAG_ENABLE_MEMPOINT`） |
| test_logger.cpp | 日志模块（LogSink + 堆栈追踪） |

### CLI 层

| 文件 | 测试目标 |
|------|---------|
| test_cli_config.cpp | CLI 配置解析 |
| test_cli_config_extra.cpp | CLI 配置扩展 |
| test_cli_mempoint.cpp | mempoint 命令解析与展示（需 `SPDIAG_ENABLE_MEMPOINT`） |
| test_frame_filter.cpp | 栈帧过滤 |
| test_display_engine.cpp | 显示引擎 |
| test_display_engine_cache.cpp | 缓存显示引擎 |
| test_sort_engine.cpp | 多键排序 |
| test_csv_writer.cpp | CSV 落盘 |

### 集成测试

| 文件 | 测试目标 |
|------|---------|
| test_integration_normal.cpp | 正常流程 |
| test_integration_concurrency.cpp | 并发场景 |
| test_integration_boundary.cpp | 边界条件 |

### 压力测试工具（独立可执行，非 GTest）

| 文件 | 用途 |
|------|------|
| test_dump_conflict_worker.cpp | 轻量工作进程，注册为独立程序，定时 PerfPoint 写入 |
| test_dump_conflict_reader.cpp | 独立读取器，统计 retry 次数和分布 |
| run_dump_conflict_test.sh | 一键编排脚本，启动多 worker + reader |

运行方式：

```bash
# 先编译压力测试工具
cmake --build build --target test_dump_conflict_worker
cmake --build build --target test_dump_conflict_reader

# 运行（8 程序，每程序 4 线程，60 秒，500ms 转储间隔）
./scripts/run_dump_conflict_test.sh 8 4 60 500 500
```
