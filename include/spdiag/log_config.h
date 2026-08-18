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
