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
    futures.reserve(100);
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

TEST(ThreadPoolTest, ExcessiveThreadCountClamped) {
    // 防线程风暴：超限配置被收敛（上限 256），池仍可用
    ThreadPool pool(100000);
    auto fut = pool.submit([] { return 7; });
    EXPECT_EQ(fut.get(), 7);
}

TEST(ThreadPoolTest, ZeroThreadCountFallsBack) {
    // 0 线程配置回退默认 8
    ThreadPool pool(0);
    auto fut = pool.submit([] { return 3; });
    EXPECT_EQ(fut.get(), 3);
}

TEST(ThreadPoolTest, MoveOnlyCaptureSupported) {
    ThreadPool pool(2);
    // 参数包路径：move-only 参数
    auto fut = pool.submit([](std::unique_ptr<int> p) { return *p; }, std::make_unique<int>(42));
    EXPECT_EQ(fut.get(), 42);
    // 捕获变量路径：move-only lambda 整体
    auto u = std::make_unique<int>(7);
    auto fut2 = pool.submit([u = std::move(u)] { return *u; });
    EXPECT_EQ(fut2.get(), 7);
}

TEST(ThreadPoolTest, ConcurrentSubmitStress) {
    ThreadPool pool(8);
    std::atomic<int> counter{0};
    constexpr int kThreads = 8;
    constexpr int kPerThread = 200;
    std::vector<std::thread> producers;
    producers.reserve(kThreads);
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
