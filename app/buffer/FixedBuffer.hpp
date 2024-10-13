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

#ifndef TINYWEBSERVER_FIXEDBUFFER_HPP
#define TINYWEBSERVER_FIXEDBUFFER_HPP

#include <cstddef>
#include <cstring>

inline static constexpr size_t SmallBufferSize = 4096;
inline static constexpr size_t LargeBufferSize = 4096 * 1000;

template<const size_t SIZE>
class FixedBuffer {
    char data_[SIZE]{};
    char *cur_;

    [[nodiscard]]
    const char *end() const {
        return data_ + sizeof(data_);
    }

public:
    FixedBuffer(): cur_(data_) {
    }

    ~FixedBuffer() = default;

    // 禁止拷贝
    FixedBuffer(const FixedBuffer &) = delete;

    FixedBuffer &operator=(const FixedBuffer &) = delete;

    [[nodiscard]]
    size_t writableBytes() const {
        return end() - cur_;
    }

    void append(const char *buf, size_t len) {
        if (writableBytes() < len) {
            len = writableBytes();
            // FIXME: 处理 buf 截断之后的部分
        }
        memcpy(cur_, buf, len);
        cur_ += len;
    }

    void clear() {
        memset(data_, 0, sizeof(data_));
        cur_ = data_;
    }

    void add(const size_t len) {
        cur_ += len;
    }

    [[nodiscard]]
    const char *data() const {
        return data_;
    }

    [[nodiscard]]
    size_t len() const {
        return cur_ - data_;
    }

    [[nodiscard]]
    char *peek() const {
        return cur_;
    }
};


#endif //TINYWEBSERVER_FIXEDBUFFER_HPP
