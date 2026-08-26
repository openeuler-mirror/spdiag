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

#include "async_worker.h"
#include "spdiag/log_sink.h"
#include "spdiag/log_config.h"

#include <chrono>
#include <iostream>

namespace SpDiag {

namespace {
// 后台线程的常规刷盘间隔
constexpr auto kFlushInterval = std::chrono::milliseconds(100);

// 队列满时业务线程的等待时长：从编译期宏 SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS 转换。
// timeout = 0ms 时 wait_for 会立即返回（等价于非阻塞检查），无需特殊分支。
constexpr auto kEnqueueTimeout =
    std::chrono::milliseconds(SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS);
} // anonymous namespace

AsyncLogWorker::AsyncLogWorker(size_t capacity, SinkGetter get_sinks,
                               std::chrono::milliseconds summary_interval)
    : capacity_(capacity),
      get_sinks_(std::move(get_sinks)),
      summary_interval_(summary_interval),
      interval_start_(std::chrono::steady_clock::now()) {}

AsyncLogWorker::~AsyncLogWorker() {
    StopAndFlush();
}

void AsyncLogWorker::Start() {
    if (running_.exchange(true)) {
        return;  // 已启动
    }
    stopping_.store(false);
    flush_requested_.store(false);
    worker_thread_ = std::thread(&AsyncLogWorker::WorkerLoop, this);
}

bool AsyncLogWorker::Enqueue(std::string&& message) {
    if (stopping_.load()) {
        return false;
    }

    std::unique_lock<std::mutex> lock(queue_mutex_);
    if (stopping_.load()) {
        return false;
    }
    if (queue_.size() >= capacity_) {
        // 短暂等待后台消费
        cv_not_full_.wait_for(lock, kEnqueueTimeout,
            [this] { return queue_.size() < capacity_ || stopping_.load(); });
        // wait_for 返回后必须二次检查：多个线程同时超时醒来会并发 push
        if (stopping_.load()) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            // 二次检查仍满，丢弃。
            // 累计计数 dropped_ 永不重置（供 DroppedCount() API 观测长期异常）；
            // 首次丢弃立即告警一次（first_drop_warned_ 0→1），避免每次丢弃刷屏；
            // 区间计数 interval_dropped_ 由 WorkerLoop 每 summary_interval_ 汇总一次。
            size_t cumulative = dropped_.fetch_add(1) + 1;
            interval_dropped_.fetch_add(1, std::memory_order_relaxed);
            if (!first_drop_warned_.exchange(true)) {
                std::cerr << "[SpDiag] WARNING: first log message dropped "
                          << "(cumulative " << cumulative
                          << ") due to queue overflow\n";
            }
            return false;
        }
    }

    queue_.push(std::move(message));
    enqueued_.fetch_add(1);
    cv_not_empty_.notify_one();
    return true;
}

size_t AsyncLogWorker::QueueSize() const {
    std::lock_guard<std::mutex> lock(queue_mutex_);
    return queue_.size();
}

void AsyncLogWorker::StopAndFlush() {
    if (!running_.exchange(false)) {
        return;
    }
    stopping_.store(true);
    cv_not_empty_.notify_all();   // 唤醒后台线程处理剩余
    cv_not_full_.notify_all();    // 唤醒所有等待入队的线程
    cv_flush_done_.notify_all();  // 唤醒可能阻塞在 FlushSync 的线程
    if (worker_thread_.joinable()) {
        worker_thread_.join();
    }
}

void AsyncLogWorker::FlushSync() {
    if (!running_.load() || stopping_.load()) {
        return;
    }
    std::unique_lock<std::mutex> lock(queue_mutex_);
    flush_requested_.store(true);
    cv_not_empty_.notify_one();
    // 同步刷盘总超时：防止在持续高压（生产 > 消费）下队列永不为空、
    // 谓词恒 false 导致调用方（特别是 SignalWatcher / 退出路径）无限阻塞。
    // 参考：spdlog async_logger 的 flush 同样是阻塞的，但其调用路径不在
    // 信号/退出关键路径；我们的 FlushSync 可能被信号处理调用，必须有兜底。
    // 超时后放弃等待并通过 stderr 告警（不能用日志系统本身，会递归）。
    constexpr auto kFlushSyncTimeout = std::chrono::seconds(3);
    constexpr auto kFlushSyncPoll = std::chrono::milliseconds(20);
    auto deadline = std::chrono::steady_clock::now() + kFlushSyncTimeout;
    while (true) {
        if (stopping_.load()) {
            return;
        }
        if (!flush_requested_.load()) {
            if (queue_.empty()) {
                return;
            }
            // flush_requested_ 已被消费但队列非空（期间又有新消息入队），重新请求
            flush_requested_.store(true);
            cv_not_empty_.notify_one();
        }
        if (cv_flush_done_.wait_for(lock, kFlushSyncPoll, [this] {
                return stopping_.load() ||
                       (!flush_requested_.load() && queue_.empty());
            })) {
            return;
        }
        if (std::chrono::steady_clock::now() >= deadline) {
            // 放弃等待：队列仍有积压无法在超时内刷完
            std::cerr << "[SpDiag] FlushSync timed out after "
                      << std::chrono::duration_cast<std::chrono::milliseconds>(
                             kFlushSyncTimeout).count()
                      << "ms; some log messages may not be persisted\n";
            return;
        }
    }
}

void AsyncLogWorker::WorkerLoop() {
    auto last_flush = std::chrono::steady_clock::now();

    while (true) {
        std::queue<std::string> batch;

        {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_not_empty_.wait(lock, [this] {
                return !queue_.empty() || stopping_.load()
                       || flush_requested_.load();
            });

            // 批量取出，swap 使锁持有时间最短
            batch.swap(queue_);
            cv_not_full_.notify_all();
        }

        // 批量写入（锁外执行 I/O）
        // get_sinks_ 返回 shared_ptr 快照，避免 AddSink 扩容导致引用悬空
        // try-catch 防止单个 sink 异常导致后台线程终止、后续日志全丢。
        // 不能用日志系统本身记录异常（会递归），改用 std::cerr。
        auto sinks = get_sinks_();
        while (!batch.empty()) {
            const std::string& front = batch.front();
            for (auto& sink : sinks) {
                try {
                    sink->Write(front);
                } catch (const std::exception& e) {
                    std::cerr << "[SpDiag] sink->Write exception: "
                              << e.what() << "\n";
                } catch (...) {
                    std::cerr << "[SpDiag] sink->Write unknown exception\n";
                }
            }
            batch.pop();
        }

        // 丢弃区间速率汇总：每 summary_interval_ 输出一条"本区间丢弃数 + 速率"。
        // worker 单线程访问，非热路径；无丢弃的区间自动静默（不刷屏）。
        // 仅在 worker 因消息/刷盘被唤醒时执行，高压丢弃期间 worker 持续活跃，
        // 汇总得以按节奏输出；丢弃停止后剩余区间延迟到下次活动再补报。
        auto now = std::chrono::steady_clock::now();
        if (now - interval_start_ >= summary_interval_) {
            double secs = std::chrono::duration<double>(now - interval_start_).count();
            size_t dropped_in_interval = interval_dropped_.exchange(0);
            interval_start_ = now;
            if (dropped_in_interval > 0) {
                size_t cumulative = dropped_.load();
                double rate = dropped_in_interval / secs;
                // 间隔 >= 1s 用秒显示，否则用毫秒（测试可注入小间隔）
                std::string interval_desc;
                if (secs >= 1.0) {
                    interval_desc = std::to_string(static_cast<int>(secs)) + "s";
                } else {
                    interval_desc =
                        std::to_string(
                            std::chrono::duration_cast<std::chrono::milliseconds>(
                                summary_interval_).count()) + "ms";
                }
                std::cerr << "[SpDiag] WARNING: " << dropped_in_interval
                          << " log messages dropped in last " << interval_desc
                          << " (total " << cumulative << ", rate ~" << rate
                          << "/s, queue cap " << capacity_ << ")\n";
            }
        }

        // 刷盘决策：时间阈值 或 FlushSync 请求。
        // 仅当队列已空时才响应 flush_requested_，避免队列仍有积压时
        // 过早通知 FlushSync 导致后续消息无人唤醒（死锁）。
        // 注意：此处不清除 flush_requested_——必须在落盘完成后再清除，
        // 否则 FlushSync 的谓词 (!flush_requested_ && queue_.empty())
        // 会在真正落盘前变为 true，导致 FlushSync 提前返回（同步刷盘语义被破坏）。
        bool time_to_flush = (now - last_flush >= kFlushInterval);
        bool should_flush = time_to_flush;
        {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.empty() && flush_requested_.load()) {
                should_flush = true;
            }
        }

        if (should_flush) {
            for (auto& sink : sinks) {
                try {
                    sink->Flush();
                } catch (const std::exception& e) {
                    std::cerr << "[SpDiag] sink->Flush exception: "
                              << e.what() << "\n";
                } catch (...) {
                    std::cerr << "[SpDiag] sink->Flush unknown exception\n";
                }
            }
            last_flush = now;

            // 落盘完成后再清除 flush_requested_ 并通知 FlushSync 等待方，
            // 确保唤醒（或谓词求值）时数据已真正落盘。
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (flush_requested_.load()) {
                flush_requested_.store(false);
                cv_flush_done_.notify_all();
            }
        }

        if (stopping_.load()) {
            std::lock_guard<std::mutex> lock(queue_mutex_);
            if (queue_.empty()) {
                break;  // 队列空且停止，退出
            }
            // 队列非空：继续循环处理剩余
        }
    }

    // 退出前最终刷盘
    auto sinks = get_sinks_();
    for (auto& sink : sinks) {
        try {
            sink->Flush();
        } catch (const std::exception& e) {
            std::cerr << "[SpDiag] sink->Flush exception: "
                      << e.what() << "\n";
        } catch (...) {
            std::cerr << "[SpDiag] sink->Flush unknown exception\n";
        }
    }
}

} // namespace SpDiag
