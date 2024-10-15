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

#ifndef TINYWEBSERVER_THREADPOOL_HPP
#define TINYWEBSERVER_THREADPOOL_HPP

#include <atomic>
#include <condition_variable>
#include <functional>
#include <future>
#include <mutex>
#include <thread>
#include <vector>

#include "BlockingQueue.hpp"

class ThreadPool {
    std::atomic<bool> shutdown_ = false;
    BlockingQueue<std::function<void()> > queue_;
    std::vector<std::thread> threads_;
    std::mutex mutex_;
    std::condition_variable cv_;

    class ThreadWorker {
        ThreadPool *pool_;

    public:
        ThreadWorker() = delete;

        explicit ThreadWorker(ThreadPool *pool) :
                pool_(pool) {
        }

        void operator()() const {
            while (!pool_->shutdown_) {
                std::unique_lock lock(pool_->mutex_);
                if (pool_->queue_.empty()) {
                    pool_->cv_.wait(lock, [this] {
                        return !pool_->queue_.empty() || pool_->shutdown_;
                    });
                    if (pool_->shutdown_) {
                        break;
                    }
                }
                auto func = pool_->queue_.pop();
                lock.unlock();
                func();
            }
        }
    };

public:
    ThreadPool() :
            ThreadPool(std::thread::hardware_concurrency() * 2) {
    }

    explicit ThreadPool(const size_t nThreads) :
            threads_(nThreads > 0 ? nThreads : 8) {
        for (auto &thread: threads_) {
            thread = std::thread(ThreadWorker(this));
        }
    }

    ThreadPool(const ThreadPool &) = delete;

    ThreadPool(ThreadPool &&) = delete;

    ThreadPool &operator=(const ThreadPool &) = delete;

    ThreadPool &operator=(ThreadPool &&) = delete;

    void shutdown() {
        shutdown_.store(true, std::memory_order_relaxed);
        cv_.notify_all();

        for (auto &thread: threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    // 提交要由线程池异步执行的函数
    // C++17之后，不建议使用decltype(f(args...))来推导函数返回值类型，因为std::result_of已经被弃用，建议使用std::invoke_result
    template<typename F, typename... Args>
    auto submit(F &&f, Args &&... args) -> std::future<std::invoke_result_t<F, Args...> > {
        // 创建一个准备执行的函数对象
        std::function<std::invoke_result_t<F, Args...>()> func = std::bind(
                std::forward<F>(f), std::forward<Args>(args)...
        );
        // 将其封装到共享的智能指针中，以便能够复制、构造、分配
        auto taskPtr = std::make_shared<std::packaged_task<std::invoke_result_t<F, Args...>()> >(func);
        // 将封装的任务包装到 void 函数中
        const std::function<void()> wrapperFunc = [taskPtr] {
            (*taskPtr)();
        };
        // 将包装后的通用函数添加到队列中
        queue_.push(wrapperFunc);
        // 唤醒一个正在等待的线程
        cv_.notify_one();
        // 返回future
        return taskPtr->get_future();
    }
};


#endif //TINYWEBSERVER_THREADPOOL_HPP
