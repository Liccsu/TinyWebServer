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

#include "Buffer.hpp"

#include <algorithm>
#include <atomic>
#include <cassert>
#include <string>
#include <sys/uio.h>

// 扩展缓冲区空间大小
void Buffer::extend(const size_t len) {
    if (writableSize() + preAllocateSize() < len) {
        buffer_.resize(writeOffset_ + len + 1);
    } else {
        const size_t readableBytes = readableSize();
        std::copy(beginPtr() + readOffset_, beginPtr() + writeOffset_, beginPtr());
        readOffset_ = 0;
        writeOffset_ = readableBytes;
        assert(readableBytes == readableSize());
    }
}

// 追加数据到缓冲区
void Buffer::append(const void *data, const size_t len) {
    if (data) {
        if (len > writableSize()) {
            extend(len);
        }
        std::copy_n(static_cast<const char *>(data), len, beginWrite());
        writeOffset_ += len;
    }
}

void Buffer::append(const std::string &data) {
    if (!data.empty()) {
        append(data.c_str(), data.size());
    }
}

// 将一个缓冲区的未读数据追加到此缓冲区的未读数据之后
void Buffer::append(const Buffer &buffer) {
    append(buffer.peek(), buffer.readableSize());
}

// 从 fd 文件中读取数据到缓冲区，即往缓冲区中写入数据
auto Buffer::readFd(const int fd) -> std::tuple<ssize_t, int> {
    int err = 0;
    char stackBuffer[65536]{};
    iovec vec[2]{};
    const size_t writableBytes = writableSize();
    // 分散读，如果数据超出可写字节数，就把超出的部分先读到栈上，待重新调整缓冲区空间后再追加到缓冲区
    vec[0].iov_base = beginWrite();
    vec[0].iov_len = writableBytes;
    vec[1].iov_base = stackBuffer;
    vec[1].iov_len = sizeof(stackBuffer);

    const ssize_t len = readv(fd, vec, 2);
    if (len < 0) {
        err = errno;
    } else if (static_cast<size_t>(len) <= writableBytes) {
        writeOffset_ += len;
    } else {
        writeOffset_ = buffer_.size();
        append(stackBuffer, len - writableBytes);
    }

    return std::tuple{len, err};
}
