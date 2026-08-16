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

#include <array>
#include <cstddef>
#include <cstring>

inline static constexpr size_t SmallBufferSize = 4096;
inline static constexpr size_t LargeBufferSize = static_cast<size_t>(4096) * 1000;

template <const size_t SIZE> class FixedBuffer {
    std::array<char, SIZE> data_{};
    char* cur_;

    [[nodiscard]]
    const char* end() const {
        return data_.data() + data_.size();
    }

public:
    FixedBuffer()
        : cur_(data_.data()) {}

    ~FixedBuffer() = default;

    // 禁止拷贝
    FixedBuffer(const FixedBuffer&) = delete;

    FixedBuffer& operator=(const FixedBuffer&) = delete;

    [[nodiscard]]
    size_t writableBytes() const {
        return end() - cur_;
    }

    void append(const char* buf, size_t len) {
        if (writableBytes() < len) {
            len = writableBytes();
            // FIXME: 处理 buf 截断之后的部分
        }
        memcpy(cur_, buf, len);
        cur_ += len;
    }

    void clear() {
        memset(data_.data(), 0, data_.size());
        cur_ = data_.data();
    }

    void add(const size_t len) { cur_ += len; }

    [[nodiscard]]
    const char* data() const {
        return data_.data();
    }

    [[nodiscard]]
    size_t len() const {
        return cur_ - data_.data();
    }

    [[nodiscard]]
    char* peek() const {
        return cur_;
    }
};

#endif // TINYWEBSERVER_FIXEDBUFFER_HPP
