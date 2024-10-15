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

#ifndef TINYWEBSERVER_BUFFER_HPP
#define TINYWEBSERVER_BUFFER_HPP

#include <atomic>
#include <cassert>
#include <cstring>
#include <string>
#include <tuple>
#include <vector>

class Buffer {
    std::vector<char> buffer_;
    std::atomic<size_t> readOffset_{};
    std::atomic<size_t> writeOffset_{};

    [[nodiscard]]
    char *beginPtr() {
        return &buffer_[0];
    }

    [[maybe_unused]]
    [[nodiscard]]
    const char *beginPtr() const {
        return &buffer_[0];
    }

    // 扩展缓冲区空间大小
    void extend(size_t len);

public:
    Buffer() :
            Buffer(64 * 1024) {
    }

    explicit Buffer(const size_t initSize) :
            buffer_(initSize) {
    }

    // 禁止拷贝
    Buffer(const Buffer &) = delete;

    Buffer &operator=(const Buffer &) = delete;

    ~Buffer() = default;

    // 已写的减去已读的，剩下就是未读的
    [[nodiscard]]
    size_t readableSize() const {
        return writeOffset_ - readOffset_;
    }

    // 缓冲区总大小减去已经写过的，剩下是空闲可写的
    [[nodiscard]]
    size_t writableSize() const {
        return buffer_.size() - writeOffset_;
    }

    // 已经读过了的就没用了，属于可预分配空间
    [[nodiscard]]
    size_t preAllocateSize() const {
        return readOffset_;
    }

    // 追加数据到缓冲区
    void append(const void *data, size_t len);

    void append(const std::string &data);

    // 将一个缓冲区的未读数据追加到此缓冲区的未读数据之后
    void append(const Buffer &buffer);

    // 从fd文件中读取数据到缓冲区
    auto readFd(int fd) -> std::tuple<ssize_t, int>;

    // 可写区域起始地址
    [[nodiscard]]
    const char *beginWrite() const {
        return &buffer_[writeOffset_];
    }

    [[nodiscard]]
    char *beginWrite() {
        return &buffer_[writeOffset_];
    }

    // 读指针的位置
    [[nodiscard]]
    const char *peek() const {
        return &buffer_[readOffset_];
    }

    // 读取 len 长度，移动读下标
    void retrieve(const size_t len) {
        readOffset_ += len;
    }

    // 读到 end 位置，移动读下标
    void retrieveUntil(const char *end) {
        assert(peek() <= end);
        retrieve(end - peek());
    }

    // 取出所有数据，buffer 归零，读写下标归零
    void retrieveAll() {
        memset(&buffer_[0], 0, buffer_.size());
        readOffset_ = writeOffset_ = 0;
    }
};


#endif //TINYWEBSERVER_BUFFER_HPP
