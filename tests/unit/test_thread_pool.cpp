#include <gtest/gtest.h>

#include <atomic>
#include <future>
#include <stdexcept>
#include <thread>
#include <vector>

#include "concurrency/ThreadPool.hpp"

using namespace std::chrono_literals;

TEST(ThreadPoolTest, ExecutesSubmittedTasks) {
    ThreadPool pool(4);
    std::atomic<int> counter{0};
    std::vector<std::future<void>> futures;
    for (int i = 0; i < 100; ++i) {
        futures.push_back(pool.submit([&] { counter.fetch_add(1); }));
    }
    for (auto& f : futures) {
        f.get();
    }
    EXPECT_EQ(counter.load(), 100);
}

TEST(ThreadPoolTest, ReturnValues) {
    ThreadPool pool(4);
    auto fut = pool.submit([] { return 42; });
    EXPECT_EQ(fut.get(), 42);
}

TEST(ThreadPoolTest, ExceptionIsolation) {
    ThreadPool pool(4);
    auto bad = pool.submit([]() -> void { throw std::runtime_error("boom"); });
    auto good = pool.submit([] { return 7; });
    // 异常被捕获在 future 中，worker 线程不退出
    EXPECT_THROW(bad.get(), std::runtime_error);
    EXPECT_EQ(good.get(), 7);
    // 池仍可用
    auto after = pool.submit([] { return 1; });
    EXPECT_EQ(after.get(), 1);
}

TEST(ThreadPoolTest, ShutdownDrainsQueue) {
    ThreadPool pool(2);
    std::atomic<int> counter{0};
    // 提交一批任务后立即 shutdown：队列中的任务应被执行完（排空）
    for (int i = 0; i < 50; ++i) {
        pool.submit([&] { counter.fetch_add(1); });
    }
    pool.shutdown();
    EXPECT_EQ(counter.load(), 50);
}

TEST(ThreadPoolTest, DestructorShutsDown) {
    // 析构自动 shutdown，不会 std::terminate
    {
        ThreadPool pool(4);
        for (int i = 0; i < 10; ++i) {
            pool.submit([] { std::this_thread::sleep_for(1ms); });
        }
    } // 析构：join 全部线程
    EXPECT_TRUE(true);
}

TEST(ThreadPoolTest, SubmitAfterShutdownReturnsInvalidFuture) {
    ThreadPool pool(2);
    pool.shutdown();
    auto fut = pool.submit([] { return 1; });
    EXPECT_FALSE(fut.valid());
}

TEST(ThreadPoolTest, ConcurrentSubmitStress) {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::vector<std::thread> producers;
    for (int t = 0; t < kThreads; ++t) {
        producers.emplace_back([&] {
            for (int i = 0; i < kPerThread; ++i) {
                auto fut = pool.submit([&] { counter.fetch_add(1); });
                fut.wait();
            }
        });
    }
    for (auto& p : producers) {
        p.join();
    }
    EXPECT_EQ(counter.load(), kThreads * kPerThread);
    pool.shutdown();
}
