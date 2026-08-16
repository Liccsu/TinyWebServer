#include <gtest/gtest.h>

#include <atomic>
#include <chrono>
#include <thread>

#include "timer/TimerHeap.hpp"

using namespace std::chrono_literals;

TEST(TimerHeapTest, EmptyHeapPeekReturnsMinusOne) {
    TimerHeap heap;
    EXPECT_EQ(heap.peek(), -1);
}

TEST(TimerHeapTest, AddAndPeekOrdering) {
    TimerHeap heap;
    heap.addTimer(1, 5000, [] {});
    heap.addTimer(2, 1000, [] {});
    heap.addTimer(3, 3000, [] {});
    // 最近到期的定时器在堆顶；peek 返回其剩余毫秒
    const int64_t ms = heap.peek();
    EXPECT_GT(ms, 0);
    EXPECT_LE(ms, 1000);
}

TEST(TimerHeapTest, TickFiresExpiredCallbacks) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    heap.addTimer(1, 10, [&] { fired.fetch_add(1); });
    heap.addTimer(2, 10, [&] { fired.fetch_add(1); });
    std::this_thread::sleep_for(30ms);
    heap.tick();
    EXPECT_EQ(fired.load(), 2);
    EXPECT_EQ(heap.peek(), -1);
}

TEST(TimerHeapTest, TickDoesNotFireFutureCallbacks) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    heap.addTimer(1, 10000, [&] { fired.fetch_add(1); });
    heap.tick();
    EXPECT_EQ(fired.load(), 0);
    EXPECT_GT(heap.peek(), 0);
}

TEST(TimerHeapTest, ResetTimerPostponesExpiration) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    heap.addTimer(1, 20, [&] { fired.fetch_add(1); });
    std::this_thread::sleep_for(5ms);
    heap.resetTimer(1, 20000);
    std::this_thread::sleep_for(30ms);
    heap.tick();
    EXPECT_EQ(fired.load(), 0); // 被推迟后未到期
    EXPECT_GT(heap.peek(), 0);
}

TEST(TimerHeapTest, RemoveTimer) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    heap.addTimer(1, 10, [&] { fired.fetch_add(1); });
    EXPECT_TRUE(heap.removeTimer(1));
    // 重复删除幂等
    EXPECT_FALSE(heap.removeTimer(1));
    std::this_thread::sleep_for(30ms);
    heap.tick();
    EXPECT_EQ(fired.load(), 0);
    EXPECT_EQ(heap.peek(), -1);
}

TEST(TimerHeapTest, CallbackRemovingItselfIsSafe) {
    // 回归：回调中删除自身定时器后 tick 不能误删其他节点
    TimerHeap heap;
    std::atomic<int> otherFired{0};
    TimerHeap* pHeap = &heap;
    heap.addTimer(1, 10, [pHeap] { pHeap->removeTimer(1); });
    heap.addTimer(2, 10, [&] { otherFired.fetch_add(1); });
    std::this_thread::sleep_for(30ms);
    heap.tick();
    // 定时器 2 仍应触发
    EXPECT_EQ(otherFired.load(), 1);
}

TEST(TimerHeapTest, CallbackRemovingOtherTimerIsSafe) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    TimerHeap* pHeap = &heap;
    heap.addTimer(1, 10, [pHeap, &fired] {
        pHeap->removeTimer(2);
        fired.fetch_add(1);
    });
    heap.addTimer(2, 10, [&] { fired.fetch_add(1); });
    heap.addTimer(3, 10, [&] { fired.fetch_add(1); });
    std::this_thread::sleep_for(30ms);
    heap.tick();
    // 1 触发并删除 2；2 不触发；3 正常触发
    EXPECT_EQ(fired.load(), 2);
}

TEST(TimerHeapTest, CallbackReAddSameIdIsSafe) {
    // 回调删除自身后又 addTimer 同 id（fd 复用边界）：不能误弹新节点
    TimerHeap heap;
    std::atomic<int> fired{0};
    TimerHeap* pHeap = &heap;
    heap.addTimer(1, 10, [pHeap, &fired] {
        pHeap->removeTimer(1);
        pHeap->addTimer(1, 10000, [&] { fired.fetch_add(1); });
        fired.fetch_add(1);
    });
    std::this_thread::sleep_for(30ms);
    heap.tick();
    EXPECT_EQ(fired.load(), 1);
    // 新定时器（10s）不应被 tick 弹出
    EXPECT_GT(heap.peek(), 0);
}

TEST(TimerHeapTest, ManyTimersConsistency) {
    TimerHeap heap;
    std::atomic<int> fired{0};
    constexpr int N = 500;
    for (int i = 1; i <= N; ++i) {
        heap.addTimer(static_cast<uint64_t>(i), 10, [&] { fired.fetch_add(1); });
    }
    // 删除一部分
    for (int i = 1; i <= N; i += 2) {
        heap.removeTimer(static_cast<uint64_t>(i));
    }
    std::this_thread::sleep_for(30ms);
    heap.tick();
    EXPECT_EQ(fired.load(), N / 2);
}
