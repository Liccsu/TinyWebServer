/*
 * Copyright (c) -2024
 * Liccsu
 * All rights reserved.
 *
 * This software is provided under the terms of the Apache License, Version 2.0.
 * Please refer to the accompanying LICENSE file for detailed information.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * Apache License for more details.
 *
 * For further inquiries, please contact:
 * liccsu@163.com
 */

#ifndef TINYWEBSERVER_BLOCKQUEUE_HPP
#define TINYWEBSERVER_BLOCKQUEUE_HPP

#include <cassert>
#include <condition_variable>
#include <limits>
#include <mutex>
#include <optional>
#include <queue>

// 有界阻塞队列（MPSC 语义）。
// shutdown() 后所有阻塞的 push/pop/front/back 立即返回：
//   - pop/front/back 返回 std::nullopt
//   - push 不再入队并返回 false
// 保证线程池在优雅退出时不会因等待空队列而挂死。
template <typename T> class BlockingQueue {
    const size_t capacity_;
    bool shutdown_ = false;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cvConsumer;
    std::condition_variable cvProducer;

public:
    explicit BlockingQueue(const size_t maxCapacity = std::numeric_limits<size_t>::max())
        : capacity_(maxCapacity) {
        assert(maxCapacity > 0);
    }

    ~BlockingQueue() { shutdown(); }

    // 队列已关闭：不可再入队，阻塞调用立即返回
    void shutdown() {
        {
            std::lock_guard lock(mutex_);
            shutdown_ = true;
        }
        cvConsumer.notify_all();
        cvProducer.notify_all();
    }

    [[nodiscard]]
    std::optional<T> front() {
        std::unique_lock lock(mutex_);
        cvConsumer.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        return queue_.front();
    }

    [[nodiscard]]
    std::optional<T> back() {
        std::unique_lock lock(mutex_);
        cvConsumer.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        return queue_.back();
    }

    bool push(const T& e) {
        std::unique_lock lock(mutex_);
        if (shutdown_) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            cvProducer.wait(lock, [this] { return shutdown_ || queue_.size() < capacity_; });
            if (shutdown_) {
                return false;
            }
        }
        queue_.emplace(e);
        cvConsumer.notify_one();
        return true;
    }

    bool push(T&& e) {
        std::unique_lock lock(mutex_);
        if (shutdown_) {
            return false;
        }
        if (queue_.size() >= capacity_) {
            cvProducer.wait(lock, [this] { return shutdown_ || queue_.size() < capacity_; });
            if (shutdown_) {
                return false;
            }
        }
        queue_.emplace(std::forward<T>(e));
        cvConsumer.notify_one();
        return true;
    }

    std::optional<T> pop() {
        std::unique_lock lock(mutex_);
        cvConsumer.wait(lock, [this] { return shutdown_ || !queue_.empty(); });
        if (queue_.empty()) {
            return std::nullopt;
        }
        T value = queue_.front();
        queue_.pop();
        cvProducer.notify_one();
        return value;
    }

    [[nodiscard]]
    bool empty() {
        std::lock_guard lock(mutex_);
        return queue_.empty();
    }

    [[nodiscard]]
    size_t size() {
        std::lock_guard lock(mutex_);
        return queue_.size();
    }
};

#endif // TINYWEBSERVER_BLOCKQUEUE_HPP
