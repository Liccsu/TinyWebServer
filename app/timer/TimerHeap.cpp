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

#include "TimerHeap.hpp"

#include <algorithm>
#include <cassert>

void TimerHeap::remove(const size_t index) {
    assert(index >= 0 && index < heap_.size());
    // 将要删除的结点换到队尾，然后调整堆
    // 如果就在队尾，就不用移动了
    if (index < heap_.size() - 1) {
        swap(index, heap_.size() - 1);

        const size_t idx = refMap_[index];
        if (const auto parentIndexOpt = parent(idx);
            parentIndexOpt.has_value() && heap_[idx] < heap_[parentIndexOpt.value()]) {
            bubbleUp(idx);
        } else {
            bubbleDown(idx);
        }
    }
    refMap_.erase(heap_.back().id);
    heap_.pop_back();
}

void TimerHeap::bubbleUp(size_t index) {
    assert(index >= 0 && index < heap_.size());
    // size_t parent = (index - 1) / 2;
    // while (static_cast<ssize_t>(parent) >= 0) {
    //     if (heap_[parent] > heap_[index]) {
    //         swap(index, parent);
    //         index = parent;
    //         parent = (index - 1) / 2;
    //     } else {
    //         break;
    //     }
    // }
    while (index > 0) {
        const auto parentIndexOpt = parent(index);
        if (!parentIndexOpt.has_value() || !(heap_[index] < heap_[parentIndexOpt.value()])) {
            break;
        }
        swap(index, parentIndexOpt.value());
        index = parentIndexOpt.value();
    }
}

void TimerHeap::bubbleDown(size_t index) {
    assert(index >= 0 && index < heap_.size());
    const size_t size = heap_.size();
    while (true) {
        size_t smallest = index;
        const size_t left = leftChild(index);
        const size_t right = rightChild(index);

        if (left < size && heap_[left] < heap_[smallest]) {
            smallest = left;
        }

        if (right < size && heap_[right] < heap_[smallest]) {
            smallest = right;
        }

        if (smallest == index) {
            break;
        }

        swap(index, smallest);
        index = smallest;
    }
}

// bool TimerHeap::bubbleDown(const size_t index, const size_t n) {
//     assert(i >= 0 && i < heap_.size());
//     // n:共几个结点
//     assert(n >= 0 && n <= heap_.size());
//     size_t idx = index;
//     size_t child = 2 * idx + 1;
//     while (child < n) {
//         if (child + 1 < n && heap_[child + 1] < heap_[child]) {
//             child++;
//         }
//         if (heap_[child] < heap_[idx]) {
//             swap(idx, child);
//             idx = child;
//             child = 2 * child + 1;
//         } else {
//             break;
//         }
//     }
//     return idx > index;
// }

void TimerHeap::swap(const size_t i, const size_t j) {
    assert(i >= 0 && i < heap_.size());
    assert(j >= 0 && j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    // 结点内部id所在索引位置也要变化
    refMap_[heap_[i].id] = i;
    refMap_[heap_[j].id] = j;
}

void TimerHeap::resetTimer(const uint64_t id, const int64_t newExpiration) {
    assert(!heap_.empty() && refMap_.contains(id));
    const size_t idx = refMap_[id];
    heap_[idx].expiration = Clock::now() + std::chrono::milliseconds(newExpiration);
    if (const auto parentIndexOpt = parent(idx);
        parentIndexOpt.has_value() && heap_[idx] < heap_[parentIndexOpt.value()]) {
        bubbleUp(idx);
    } else {
        bubbleDown(idx);
    }
}

uint64_t TimerHeap::addTimer(const uint64_t id, const int64_t timeout, const TimeoutCallback &timeoutCallback) {
    assert(id >= 0);
    // 如果有，则调整
    if (refMap_.contains(id)) {
        const size_t idx = refMap_[id];
        heap_[idx].expiration = Clock::now() + std::chrono::milliseconds(timeout);
        heap_[idx].timeoutCallback = timeoutCallback;
        if (const auto parentIndexOpt = parent(idx);
            parentIndexOpt.has_value() && heap_[idx] < heap_[parentIndexOpt.value()]) {
            bubbleUp(idx);
        } else {
            bubbleDown(idx);
        }
    } else {
        const size_t idx = heap_.size();
        refMap_[id] = idx;
        heap_.push_back({
            id,
            Clock::now() + std::chrono::milliseconds(timeout),
            timeoutCallback
        });
        bubbleUp(idx);
    }

    return id;
}

void TimerHeap::clear() {
    refMap_.clear();
    heap_.clear();
}

void TimerHeap::tick() {
    // 清除超时结点
    if (heap_.empty()) {
        return;
    }
    while (!heap_.empty()) {
        auto [id, expiration, timeoutCallback] = heap_.front();
        if (std::chrono::duration_cast<std::chrono::milliseconds>(expiration - Clock::now()).count() > 0) {
            break;
        }
        timeoutCallback();
        pop();
    }
}

void TimerHeap::pop() {
    assert(!heap_.empty());
    remove(0);
}

int64_t TimerHeap::peek() {
    tick();
    if (!heap_.empty()) {
        int64_t res = std::chrono::duration_cast<std::chrono::milliseconds>(heap_.front().expiration - Clock::now()).
                count();
        if (res < 0) {
            res = 0;
        }

        return res;
    }
    return -1;
}
