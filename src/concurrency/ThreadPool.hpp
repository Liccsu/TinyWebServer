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

#ifndef TINYWEBSERVER_THREADPOOL_HPP
#define TINYWEBSERVER_THREADPOOL_HPP

#include <atomic>
#include <functional>
#include <future>
#include <thread>
#include <vector>

#include "BlockingQueue.hpp"

// 固定线程数线程池。
// 生命周期约束：
//   - 析构自动 shutdown()（排空队列 + join 全部线程），杜绝 std::terminate；
//   - shutdown() 之后不得再 submit()（提交的任务不会被执行，future 永不就绪）。
// 异常隔离：任务抛出的异常保存在 std::future 中，由调用方在 future.get() 时处理，
// 不会导致 worker 线程退出。
class ThreadPool {
    std::atomic<bool> shutdown_{false};
    BlockingQueue<std::function<void()>> queue_;
    std::vector<std::thread> threads_;

    class ThreadWorker {
        ThreadPool* pool_;

    public:
        ThreadWorker() = delete;

        explicit ThreadWorker(ThreadPool* pool)
            : pool_(pool) {}

        void operator()() const {
            // 循环只依赖 pop() 的 shutdown 语义退出：
            // shutdown 后队列中已提交任务仍会被执行完（排空），队列空后 pop 返回 nullopt 退出
            while (true) {
                auto task = pool_->queue_.pop();
                if (!task) {
                    break;
                }
                (*task)();
            }
        }
    };

public:
    ThreadPool()
        : ThreadPool(std::thread::hardware_concurrency() * 2) {}

    explicit ThreadPool(const size_t nThreads)
        : threads_(nThreads > 0 ? nThreads : 8) {
        for (auto& thread : threads_) {
            thread = std::thread(ThreadWorker(this));
        }
    }

    ~ThreadPool() { shutdown(); }

    ThreadPool(const ThreadPool&) = delete;

    ThreadPool(ThreadPool&&) = delete;

    ThreadPool& operator=(const ThreadPool&) = delete;

    ThreadPool& operator=(ThreadPool&&) = delete;

    // 排空队列并停止所有工作线程。
    // 注意：shutdown 期间仍有线程在 pop() 的窗口内可能拿到新任务并执行 ——
    // 因此调用方必须先停止投递新任务，再调用 shutdown()。
    void shutdown() {
        bool expected = false;
        if (!shutdown_.compare_exchange_strong(expected, true)) {
            // 已 shutdown（并发调用）
            for (auto& thread : threads_) {
                if (thread.joinable()) {
                    thread.join();
                }
            }
            return;
        }
        queue_.shutdown();
        for (auto& thread : threads_) {
            if (thread.joinable()) {
                thread.join();
            }
        }
    }

    [[nodiscard]]
    bool isShutdown() const {
        return shutdown_.load(std::memory_order_acquire);
    }

    // 提交要由线程池异步执行的函数。
    // 返回 std::future：任务抛出的异常将在 future.get() 时重新抛出。
    // shutdown 后调用返回无效 future（任务未入队）。
    template <typename F, typename... Args>
    auto submit(F&& f, Args&&... args) -> std::future<std::invoke_result_t<F, Args...>> {
        using ReturnType = std::invoke_result_t<F, Args...>;
        auto func = std::bind(std::forward<F>(f), std::forward<Args>(args)...);
        auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(func);
        const std::function<void()> wrapperFunc = [taskPtr] { (*taskPtr)(); };
        if (!queue_.push(wrapperFunc)) {
            // 队列已 shutdown：返回永不就绪的 future
            return {};
        }
        return taskPtr->get_future();
    }
};

#endif // TINYWEBSERVER_THREADPOOL_HPP
