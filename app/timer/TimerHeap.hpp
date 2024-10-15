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

#ifndef TINYWEBSERVER_TIMERHEAP_HPP
#define TINYWEBSERVER_TIMERHEAP_HPP

#include <chrono>
#include <functional>
#include <optional>
#include <unordered_map>
#include <vector>

using TimeoutCallback = std::function<void()>;
using Clock = std::chrono::system_clock;
using TimePoint = std::chrono::system_clock::time_point;

class TimerHeap {
    struct TimerNode {
        uint64_t id;
        // 超时时间点
        TimePoint expiration;
        // 超时回调function<void()>
        TimeoutCallback timeoutCallback;

        // 重载三路比较运算符(C++20起)可以同时默认实现 > < >= <= == != 六种基本比较运算符，减少代码冗余
        // 三路比较运算符(C++20起)可以返回三种类型：
        // std::strong_ordering：强序，严格的顺序比较。隐含可替换关系，若 a 等价于 b，则 f(a) 等价于 f(b)
        //   若 a 小于 b，返回 std::strong_ordering::less
        //   若 a 大于 b，返回 std::strong_ordering::greater
        //   若 a 等于 b，返回 std::strong_ordering::equal
        // std::weak_ordering：弱序，允许等价但不严格的顺序比较。不隐含可替换关系，若 a 等价于 b，则 f(a) 不一定等价于 f(b)
        //   若 a 小于 b，返回 std::weak_ordering::less
        //   若 a 大于 b，返回 std::weak_ordering::greater
        //   若 a 等价于 b，返回 std::weak_ordering::equivalent
        // std::partial_ordering：偏序，允许部分顺序比较。既不隐含可替换关系，也允许完全不可比较关系
        //   若 a 小于 b，返回 std::partial_ordering::less
        //   若 a 大于 b，返回 std::partial_ordering::greater
        //   若 a 等价于 b，返回 std::partial_ordering::equivalent
        //   否则，返回 std::partial_ordering::unordered
        // 此处两个 std::chrono::system_clock::time_point 类型比较的返回值应为 std::strong_ordering
        auto operator<=>(const TimerNode &t) const {
            return expiration <=> t.expiration;
        }
    };

    std::vector<TimerNode> heap_;
    // 辅助哈希表，可降低堆查找的时间复杂度
    std::unordered_map<uint64_t, size_t> refMap_;

    // 获取父节点索引
    [[nodiscard]]
    static std::optional<size_t> parent(const size_t index) {
        if (index == 0) {
            return std::nullopt;
        }
        return (index - 1) / 2;
    }

    // 获取左子节点索引
    [[nodiscard]]
    static size_t leftChild(const size_t index) {
        return 2 * index + 1;
    }

    // 获取右子节点索引
    [[nodiscard]]
    static size_t rightChild(const size_t index) {
        return 2 * index + 2;
    }

    void remove(size_t index);

    void bubbleUp(size_t index);

    void bubbleDown(size_t index);

    // bool bubbleDown(size_t index, size_t n);

    void swap(size_t i, size_t j);

public:
    TimerHeap() {
        heap_.reserve(1024);
    }

    ~TimerHeap() {
        clear();
    }

    void resetTimer(uint64_t id, int64_t newExpiration);

    uint64_t addTimer(uint64_t id, int64_t timeout, const TimeoutCallback &timeoutCallback);

    void clear();

    void tick();

    void pop();

    int64_t peek();
};


#endif //TINYWEBSERVER_TIMERHEAP_HPP
