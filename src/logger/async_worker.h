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

#include <string>
#include <queue>
#include <mutex>
#include <condition_variable>
#include <thread>
#include <atomic>
#include <functional>
#include <vector>
#include <memory>

namespace SpDiag {

class LogSink;  // 前向声明

/**
 * 异步日志工作线程。
 *
 * 业务线程将格式化后的消息入队（微秒级），后台线程批量出队写入 sinks，
 * 使 I/O 完全从业务线程剥离。队列满时短暂等待，超时则丢弃并计数，
 * 避免日志系统反噬业务线程。
 *
 * 线程安全：所有公共接口可被多线程并发调用。
 */
class AsyncLogWorker {
public:
    // 使用 shared_ptr 快照避免 AddSink 扩容导致引用悬空
    using SinkList = std::vector<std::shared_ptr<LogSink>>;
    using SinkGetter = std::function<SinkList()>;

    /**
     * 构造异步工作线程。
     * @param capacity  队列最大容量，满时按"短暂等待 + 丢弃"策略处理
     * @param get_sinks 返回 sinks 的 shared_ptr 快照，后台线程持快照遍历，
     *                  即使 AddSink 触发 vector 扩容，快照仍有效
     */
    explicit AsyncLogWorker(size_t capacity, SinkGetter get_sinks);
    ~AsyncLogWorker();

    AsyncLogWorker(const AsyncLogWorker&) = delete;
    AsyncLogWorker& operator=(const AsyncLogWorker&) = delete;

    /**
     * 入队一条日志消息。
     *
     * 队列满时的行为由 SPDIAG_ASYNC_ENQUEUE_TIMEOUT_MS 决定：
     *   - timeout > 0：业务线程会被阻塞至多 timeout 毫秒，等待后台线程消费；
     *                  超时仍满则丢弃该条日志并计入 DroppedCount。
     *   - timeout = 0：立即丢弃（非阻塞模式），完全避免日志路径反噬业务线程。
     *                  适用于对延迟极度敏感的高频场景。
     * 注意：在慢盘 + 持续高压写入场景下，timeout > 0 可能在本来想消除的
     *       业务线程延迟路径上叠加尖刺，务必根据实际 SLA 权衡后配置。
     *
     * @return true 入队成功；false 被丢弃（或 stopping_ 已置位）
     */
    bool Enqueue(std::string&& message);

    /** 启动后台线程。重复调用安全。 */
    void Start();

    /**
     * 停止并刷盘：通知后台线程处理完队列中剩余消息后退出。
     * 调用后不可再 Enqueue，仅用于 Shutdown。
     */
    void StopAndFlush();

    /**
     * 同步刷盘：阻塞等待队列排空并完成一次 Flush，但不停止后台线程。
     * 适用于 SignalWatcher 回调、FlushAll 等需要继续工作的场景。
     */
    void FlushSync();

    /** 获取累计丢弃的日志条数 */
    size_t DroppedCount() const { return dropped_.load(); }

    /** 获取累计成功入队的日志条数 */
    size_t EnqueuedCount() const { return enqueued_.load(); }

    /** 获取当前队列中待处理的消息数（近似值） */
    size_t QueueSize() const;

private:
    void WorkerLoop();

    size_t capacity_;
    SinkGetter get_sinks_;

    std::queue<std::string> queue_;
    mutable std::mutex queue_mutex_;
    std::condition_variable cv_not_empty_;
    std::condition_variable cv_not_full_;
    std::condition_variable cv_flush_done_;       // FlushSync 等待
    std::atomic<bool> running_{false};
    std::atomic<bool> stopping_{false};
    std::atomic<bool> flush_requested_{false};   // 请求立即刷盘
    // dropped_：累计丢弃数（自启动起永不重置），供 DroppedCount() API 暴露，
    //           保证用户能观测到"持续小量渗漏"等长期异常。
    std::atomic<size_t> dropped_{0};
    // batch_dropped_：本批次丢弃数（WorkerLoop 每次 swap 后 exchange(0) 清零），
    //           仅用于触发 kDropWarnThreshold 的汇总告警，避免单批 < 阈值时
    //           持续渗漏永远不告警。
    std::atomic<size_t> batch_dropped_{0};
    std::atomic<size_t> enqueued_{0};

    std::thread worker_thread_;
};

} // namespace SpDiag
