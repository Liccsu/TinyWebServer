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

#include "TimerHeap.hpp"

#include <algorithm>
#include <cassert>

void TimerHeap::remove(const size_t index) {
    assert(index < heap_.size());
    const uint64_t removedId = heap_[index].id;
    const size_t last = heap_.size() - 1;
    if (index != last) {
        // 将要删除的结点换到队尾
        swap(index, last);
        // 先弹出（此时 heap_[last] 即被删结点），再调整换到 index 位置的结点。
        // 注意：必须先 pop 再调整 —— 若先调整，bubble 可能把被删结点移离队尾，
        // 导致 refMap_ 删除错误条目（堆不变量破坏）。
        heap_.pop_back();
        refMap_.erase(removedId);
        if (const auto parentIndexOpt = parent(index);
            parentIndexOpt.has_value() && heap_[index] < heap_[parentIndexOpt.value()]) {
            bubbleUp(index);
        } else {
            bubbleDown(index);
        }
    } else {
        heap_.pop_back();
        refMap_.erase(removedId);
    }
}

bool TimerHeap::removeTimer(const uint64_t id) {
    const auto it = refMap_.find(id);
    if (it == refMap_.end()) {
        return false;
    }
    remove(it->second);
    return true;
}

void TimerHeap::bubbleUp(size_t index) {
    assert(index < heap_.size());
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
    assert(index < heap_.size());
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

void TimerHeap::swap(const size_t i, const size_t j) {
    assert(i < heap_.size());
    assert(j < heap_.size());
    std::swap(heap_[i], heap_[j]);
    // 结点内部 id 所在索引位置也要变化
    refMap_[heap_[i].id] = i;
    refMap_[heap_[j].id] = j;
}

void TimerHeap::resetTimer(const uint64_t id, const int64_t newExpiration) {
    const auto it = refMap_.find(id);
    if (it == refMap_.end()) {
        // 定时器可能已被移除（连接已关闭），幂等忽略
        return;
    }
    const size_t idx = it->second;
    heap_[idx].expiration = Clock::now() + std::chrono::milliseconds(newExpiration);
    if (const auto parentIndexOpt = parent(idx);
        parentIndexOpt.has_value() && heap_[idx] < heap_[parentIndexOpt.value()]) {
        bubbleUp(idx);
    } else {
        bubbleDown(idx);
    }
}

uint64_t TimerHeap::addTimer(const uint64_t id, const int64_t timeout, const TimeoutCallback& timeoutCallback) {
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
        heap_.push_back({id, Clock::now() + std::chrono::milliseconds(timeout), timeoutCallback});
        bubbleUp(idx);
    }

    return id;
}

void TimerHeap::clear() {
    refMap_.clear();
    heap_.clear();
}

void TimerHeap::tick() {
    if (heap_.empty()) {
        return;
    }
    while (!heap_.empty()) {
        const auto& front = heap_.front();
        if (front.expiration > Clock::now()) {
            break;
        }
        // 拷贝出回调再执行：回调可能修改堆（如 closeConn 删除本定时器或其他定时器）
        const uint64_t id = front.id;
        const TimePoint expiration = front.expiration;
        TimeoutCallback callback = front.timeoutCallback;
        callback();
        if (heap_.empty()) {
            break;
        }
        // 仅当堆顶仍是原节点（id 与到期时间均未变，含"删除后又加同 id 新定时器"的边界）时弹出
        if (heap_.front().id == id && heap_.front().expiration == expiration) {
            pop();
        }
        // 若回调删除了本定时器，front 已指向下一个节点，继续循环
    }
}

void TimerHeap::pop() {
    assert(!heap_.empty());
    remove(0);
}

int64_t TimerHeap::peek() {
    tick();
    if (heap_.empty()) {
        return -1;
    }
    const auto earliest = heap_.front().expiration;
    int64_t res = std::chrono::duration_cast<std::chrono::milliseconds>(earliest - Clock::now()).count();
    if (res < 0) {
        res = 0;
    }
    return res;
}
