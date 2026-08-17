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
#include <cstdint>
#include <functional>
#include <future>
#include <thread>
#include <vector>
#include <pthread.h>

#include "BlockingQueue.hpp"

// 固定线程数线程池（MPMC 任务队列）。
//
// 生命周期约束：
//   - 析构自动 shutdown()（排空队列 + join 全部线程），杜绝 std::terminate；
//   - shutdown() 之后 submit() 返回无效 future（任务未入队）；
//   - 已入队任务在 shutdown 时仍会被执行完（排空语义）。
//
// 异常隔离：任务抛出的异常保存在 std::future 中，由调用方在 future.get() 时处理，
// 不会导致 worker 线程退出。
//
// move-only 支持：任务可捕获/接收 move-only 对象（unique_ptr 等）——
// 经 std::packaged_task 移动包装 + shared_ptr 间接类型擦除，入队元素仅捕获
// shared_ptr（可拷贝），无拷贝 move-only 对象的需求。
//
// 使用约束（违反将导致错误）：
//   - 任务内不得同步等待池内其他任务的 future（worker 池满时必然死锁）。
class ThreadPool {
    // 线程数上限：防止配置错误导致线程风暴（实际服务器场景远低于此）
    static constexpr size_t MAX_THREADS = 256;

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
        : ThreadPool(static_cast<size_t>(std::thread::hardware_concurrency()) * 2) {}

    explicit ThreadPool(const size_t nThreads)
        : threads_(normalizeThreadCount(nThreads)) {
        for (auto& thread : threads_) {
            thread = std::thread(ThreadWorker(this));
            // 线程命名：便于 top/htop/gdb 辨识 worker 线程
            pthread_setname_np(thread.native_handle(), "tws-worker");
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
            // 已 shutdown（并发调用）：确保线程已回收
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
        // 快路径：已 shutdown 直接拒绝，避免抢锁（push 内部仍有最终检查，双保险）
        if (shutdown_.load(std::memory_order_acquire)) {
            return {};
        }
        // 用 lambda 捕获替代 std::bind（C++20 init-capture pack），语义更清晰；
        // boundArgs 为左值捕获，move-only 参数（如 unique_ptr）需 std::move 传入
        //（任务只执行一次，移动安全）
        auto func = [f = std::forward<F>(f), ...boundArgs = std::forward<Args>(args)]() mutable -> ReturnType {
            return std::invoke(f, std::move(boundArgs)...);
        };
        auto taskPtr = std::make_shared<std::packaged_task<ReturnType()>>(std::move(func));
        // lambda 隐式构造 std::function 为右值，走 push(T&&) 移动入队（省一次拷贝）
        if (!queue_.push([taskPtr] { (*taskPtr)(); })) {
            // 队列已 shutdown（快路径检查与 push 之间的竞态窗口）：返回永不就绪的 future
            return {};
        }
        return taskPtr->get_future();
    }

private:
    [[nodiscard]]
    static size_t normalizeThreadCount(const size_t requested) {
        if (requested == 0 || requested > MAX_THREADS) {
            return 8;
        }
        return requested;
    }
};

#endif // TINYWEBSERVER_THREADPOOL_HPP
