/*
 * Copyright (c) Huawei Technologies Co., Ltd. 2026-2026. All rights reserved
 * SpDiag is licensed under Mulan PSL v2.
 * You can use this software according to the terms and conditions of the Mulan PSL v2.
 * You may obtain a copy of Mulan PSL v2 at:
 *         http://license.coscl.org.cn/MulanPSL2
 * THIS SOFTWARE IS PROVIDED ON AN "AS IS" BASIS, WITHOUT WARRANTIES OF ANY KIND,
 * EITHER EXPRESS OR IMPLIED, INCLUDING BUT NOT LIMITED TO NON-INFRINGEMENT,
 * MERCHANTABILITY OR FIT FOR A PARTICULAR PURPOSE.
 * See the Mulan PSL v2 for more details.
 */

#pragma once

#ifndef SPDIAG_LOG_TO_FILE
#define SPDIAG_LOG_TO_FILE 0
#endif

#ifndef SPDIAG_LOG_TO_CONSOLE
#define SPDIAG_LOG_TO_CONSOLE 1
#endif

#ifndef SPDIAG_LOG_FILE_PATH
#define SPDIAG_LOG_FILE_PATH "/var/log/spdiag/spdiag.log"
#endif

#ifndef SPDIAG_LOG_MAX_SIZE
#define SPDIAG_LOG_MAX_SIZE 10485760
#endif

#ifndef SPDIAG_LOG_MAX_FILES
#define SPDIAG_LOG_MAX_FILES 5
#endif

#ifndef SPDIAG_ASYNC_QUEUE_SIZE
#define SPDIAG_ASYNC_QUEUE_SIZE 8192
#endif

// 队列满时业务线程的最长等待时间（毫秒）。
//   > 0 : 短暂等待后台线程消费，超时后丢弃；持续高压下会在该时长内阻塞业务线程。
//   = 0 : 立即丢弃（非阻塞模式），完全避免日志路径反噬业务线程。
// 注意：无论取何值，丢弃的日志均会计入 DroppedCount() 统计。
#ifndef SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS
#define SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS 1
#endif

// ERROR 级别是否绕过异步队列、在调用线程直接同步写入 sinks。
// = 1（默认）：ERROR 保证不因队列满被丢弃、调用返回前即可见，适用于关键错误审计。
// = 0：回退到"全部异步"（ERROR 也可能在高压下被丢弃）。
#ifndef SPDIAG_LOG_ERROR_SYNC
#define SPDIAG_LOG_ERROR_SYNC 1
#endif

// ERROR 同步写入后是否调用 sink->Flush()。
// 注意：这是"flush 到内核页缓存"（µs 级开销），不是 fsync 强制落盘。
// = 1（默认）：进程崩溃但 OS 存活时通常已落盘；断电/内核崩溃不保证。
// = 0：仅写（write 系统调用后不加 flush），进一步降低同步路径开销。
// 说明：真正的 fsync（审计级强保证）需要访问底层文件描述符，当前 FileSink
//       基于 std::ofstream 不暴露 fd，列为后续增强项，此处未提供开关。
#ifndef SPDIAG_LOG_ERROR_SYNC_FLUSH
#define SPDIAG_LOG_ERROR_SYNC_FLUSH 1
#endif
