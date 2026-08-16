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

#ifndef TINYWEBSERVER_LOGSTREAM_HPP
#define TINYWEBSERVER_LOGSTREAM_HPP

#include <algorithm>
#include <array>
#include <charconv>
#include <concepts>
#include <cstdint>
#include <string>
#include <type_traits>

#include "FixedBuffer.hpp"
#include "Fmt.hpp"

class LogStream {
    using LogBuffer = FixedBuffer<SmallBufferSize>;
    using Self = LogStream;

    static constexpr size_t MaxNumericSize = 48;

    static constexpr std::array<char, 20> digits = {'9', '8', '7', '6', '5', '4', '3', '2', '1', '0',
                                                    '1', '2', '3', '4', '5', '6', '7', '8', '9', '\0'};
    static_assert(digits.size() == 20, "wrong number of digits");

    static constexpr const char* const number = digits.data() + 9;

    static constexpr std::array<char, 32> digitsHex = {'F', 'E', 'D', 'C', 'B', 'A', '9', '8', '7', '6', '5',
                                                       '4', '3', '2', '1', '0', '1', '2', '3', '4', '5', '6',
                                                       '7', '8', '9', 'A', 'B', 'C', 'D', 'E', 'F', '\0'};
    static_assert(digitsHex.size() == 32, "wrong number of digitsHex");

    static constexpr const char* const numberHex = digitsHex.data() + 15;

    LogBuffer logBuffer_;

public:
    LogStream() = default;

    ~LogStream() = default;

    // 禁止拷贝
    LogStream(const Self&) = delete;

    Self& operator=(const Self&) = delete;

    LogBuffer& buffer() { return logBuffer_; }

    // 使用 C++20 的 concepts 进行泛型约束
    // 方式一：
    // template<typename T>
    // void foo(T v) requires std::integral<T> {}
    // 方式二：
    // void foo(std::integral auto v) {}

    size_t intToDecStr(char* buf, std::integral auto value) {
        using ValueType = decltype(value);
        using UnsignedType = std::make_unsigned_t<ValueType>;
        // 负数通过无符号运算避免取模得到负值（负数下标越界读 UB）；
        // INT_MIN 直接取负会溢出，采用 -(value + 1) + 1 的安全取负
        UnsignedType tmp = value;
        bool negative = false;
        if constexpr (std::signed_integral<ValueType>) {
            if (value < 0) {
                negative = true;
                tmp = UnsignedType(-(value + 1)) + 1;
            }
        }
        char* p = buf;
        do {
            const int lsd = static_cast<int>(tmp % 10);
            tmp /= 10;
            *p++ = number[lsd];
        } while (tmp != 0);
        if (negative) {
            *p++ = '-';
        }
        *p = '\0';
        std::reverse(buf, p);

        return static_cast<size_t>(p - buf);
    }

    size_t intToHexStr(char* buf, std::integral auto value) {
        using ValueType = decltype(value);
        using UnsignedType = std::make_unsigned_t<ValueType>;
        UnsignedType tmp = value;
        bool negative = false;
        if constexpr (std::signed_integral<ValueType>) {
            if (value < 0) {
                negative = true;
                tmp = UnsignedType(-(value + 1)) + 1;
            }
        }
        char* p = buf;
        do {
            const int lsd = static_cast<int>(tmp % 16);
            tmp /= 16;
            *p++ = numberHex[lsd];
        } while (tmp != 0);
        if (negative) {
            *p++ = '-';
        }
        *p = '\0';
        std::reverse(buf, p);

        return static_cast<size_t>(p - buf);
    }

    Self& operator<<(const bool value) {
        if (value) {
            logBuffer_.append("true", 4);
        } else {
            logBuffer_.append("false", 5);
        }
        return *this;
    }

    Self& operator<<(const char value) {
        logBuffer_.append(&value, 1);
        return *this;
    }

    Self& operator<<(const char* value) {
        logBuffer_.append(value, strlen(value));
        return *this;
    }

    Self& operator<<(const std::string& value) {
        logBuffer_.append(value.data(), value.size());
        return *this;
    }

    Self& operator<<(std::integral auto value) {
        if (logBuffer_.writableBytes() >= MaxNumericSize) {
            const size_t len = intToDecStr(logBuffer_.peek(), value);
            logBuffer_.add(len);
        }
        return *this;
    }

    // TODO: replace this with Grisu3 by Florian Loitsch.
    Self& operator<<(std::floating_point auto value) {
        if (logBuffer_.writableBytes() >= MaxNumericSize) {
            char* buf = logBuffer_.peek();
            // C++17 起, 结构化绑定和 std::to_chars
            auto [ptr, ec] = std::to_chars(buf, buf + MaxNumericSize, value);
            if (ec == std::errc()) {
                const size_t len = ptr - buf;
                logBuffer_.add(len);
            } else {
                // TODO: 处理转换出错情况
            }
        }
        return *this;
    }

    Self& operator<<(const void* pVoid) {
        const auto value = reinterpret_cast<uintptr_t>(pVoid);
        if (logBuffer_.writableBytes() >= MaxNumericSize) {
            char* buf = logBuffer_.peek();
            buf[0] = '0';
            buf[1] = 'x';
            const size_t len = intToHexStr(buf + 2, value) + 2;
            logBuffer_.add(len);
        }
        return *this;
    }

    Self& operator<<(const Fmt& fmt) {
        logBuffer_.append(fmt.data(), fmt.length());
        return *this;
    }
};

#endif // TINYWEBSERVER_LOGSTREAM_HPP
