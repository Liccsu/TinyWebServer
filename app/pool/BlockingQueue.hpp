/*
 * Copyright (c) -2024
 * Liccsu
 * All rights reserved.
 *
 * This software is provided under the terms of the GPL License.
 * Please refer to the accompanying LICENSE file for detailed information.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE. See the
 * GPL License for more details.
 *
 * For further inquiries, please contact:
 * liccsu@163.com
 */

#ifndef TINYWEBSERVER_BLOCKQUEUE_HPP
#define TINYWEBSERVER_BLOCKQUEUE_HPP

#include <cassert>
#include <condition_variable>
#include <mutex>
#include <queue>

template<typename T>
class BlockingQueue {
    const size_t capacity_;
    std::queue<T> queue_;
    std::mutex mutex_;
    std::condition_variable cvConsumer;
    std::condition_variable cvProducer;

public:
    explicit BlockingQueue(const size_t maxCapacity = std::numeric_limits<size_t>::max()) :
            capacity_(maxCapacity) {
        assert(maxCapacity > 0);
    }

    [[nodiscard]]
    T front() {
        std::unique_lock lock(mutex_);
        if (queue_.empty()) {
            cvConsumer.wait(lock, [this] {
                return !queue_.empty();
            });
        }
        return queue_.front();
    }

    [[nodiscard]]
    T back() {
        std::unique_lock lock(mutex_);
        if (queue_.empty()) {
            cvConsumer.wait(lock, [this] {
                return !queue_.empty();
            });
        }
        return queue_.back();
    }

    void push(const T &e) {
        std::unique_lock lock(mutex_);
        if (queue_.size() >= capacity_) {
            cvProducer.wait(lock, [this] {
                return queue_.size() < capacity_;
            });
        }
        queue_.emplace(std::move(e));
        cvConsumer.notify_one();
    }

    void push(T &&e) {
        std::unique_lock lock(mutex_);
        if (queue_.size() >= capacity_) {
            cvProducer.wait(lock, [this] {
                return queue_.size() < capacity_;
            });
        }
        queue_.emplace(std::forward<T>(e));
        cvConsumer.notify_one();
    }

    T pop() {
        std::unique_lock lock(mutex_);
        if (queue_.empty()) {
            cvConsumer.wait(lock, [this] {
                return !queue_.empty();
            });
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


#endif //TINYWEBSERVER_BLOCKQUEUE_HPP
