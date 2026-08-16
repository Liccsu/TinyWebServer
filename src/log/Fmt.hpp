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

#ifndef TINYWEBSERVER_FMT_HPP
#define TINYWEBSERVER_FMT_HPP

#include <array>
#include <cstdio>
#include <type_traits>
#include <utility>

class Fmt {
    std::array<char, 64> buf_{};
    size_t length_;

public:
    template <typename T>
    [[maybe_unused]]
    Fmt(const char* format, T&& value) {
        static_assert(std::is_arithmetic_v<T>, "Must be arithmetic type");
        length_ = std::snprintf(buf_.data(), buf_.size(), format, std::forward<T>(value));
    }

    [[nodiscard]]
    const char* data() const {
        return buf_.data();
    }

    [[nodiscard]]
    size_t length() const {
        return length_;
    }
};

#endif // TINYWEBSERVER_FMT_HPP
