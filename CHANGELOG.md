# Changelog

## [1.0.0] - 2026-08-05

SpDiag (SuperPoD Diagnostics) 1.0.0 首个稳定版：面向 Linux aarch64 嵌入式系统的 C++17 零开销性能诊断框架。

### Added

- PerfPoint：编译期 X-Macro 性能探针。
- Global PerfPoint：线程安全的进程级性能聚合。
- PerfLog：每探针最近 100 次调用的纳秒级耗时、线程 ID 与 CPU 核号记录。
- MemPoint：基于 USDT + eBPF 的点位级与字段级内存观测。
- Memstat：基于 eBPF uprobe 的无侵入式内存分配追踪。
- Cachestat：基于 perf_event 的缓存命中率监控。
- CLI：`show`/`watch`/`start`/`stop`/`status`/`clear`/`history`，支持多键排序、过滤、帧过滤与 CSV 落盘。
- 统一日志模块（终端/文件双输出）。
- 百分位计算（P99/P999/P9999）。
- RPM 包（`spdiag` / `spdiag-devel` / `spdiag-static`）与 CMake 包导出（`find_package(SpDiag)`）。
