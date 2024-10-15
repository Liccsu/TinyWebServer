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

#include "AsyncLogging.hpp"

#include <cassert>
#include <memory>

#include "LogFile.hpp"

void AsyncLogging::threadCallback() {
    assert(running_ == true);
    LogFile logFile;
    // 预分配两块 buffer，以备在临界区内交换
    BufferPtr newBuffer1(new LogBuffer);
    BufferPtr newBuffer2(new LogBuffer);
    newBuffer1->clear();
    newBuffer2->clear();
    BufferVector buffersToWrite;
    buffersToWrite.reserve(16);

    while (running_) {
        assert(newBuffer1);
        assert(newBuffer2);
        assert(buffersToWrite.empty());
        {
            std::unique_lock lock(mutex_);
            if (buffers_.empty()) {
                // 当 buffers 里没有待同步到磁盘的缓冲区时，等待条件的发生或者超时 flushInterval_
                cvBuffersEmpty_.wait_for(lock, std::chrono::seconds(flushInterval_), [&] {
                    return !buffers_.empty();
                });
            }
            // 条件发生时，将当前工作缓冲区送入 buffers 以待同步到磁盘，目的是哪怕前端在 3s 内没有写满一块缓冲区，也能及时将日志同步到磁盘
            buffers_.push_back(std::move(currentBuffer_));
            // 将预分配的空闲缓冲区用作当前工作缓冲区
            currentBuffer_ = std::move(newBuffer1);
            // 直接把 buffers 里的缓冲区都拿过来，以便后续安全地访问，避免临界区过长或在访问 buffers 的时候 buffers 又被前端改变
            buffersToWrite.swap(buffers_);
            if (!nextBuffer_) {
                // 如果预备缓冲区也用完了，则将另一块预分配的缓冲区用作预备缓冲区，保证前端始终有一个预备缓冲区可随时取用
                nextBuffer_ = std::move(newBuffer2);
            }
        }

        assert(!buffersToWrite.empty());

        if (buffersToWrite.size() > 25) {
            // 考虑到当前端拼命发送日志消息，超过后端处理能力时（一般不会），会出现日志堆积情况，直接丢弃的堆积的日志，避免日志库本身故障
            char buf[256]{};
            using namespace std::chrono;
            const auto now = system_clock::now();
            const auto tt = system_clock::to_time_t(now);
            const auto us = duration_cast<microseconds>(now.time_since_epoch()) % 1000000;

            tm tmTime{};
            localtime_r(&tt, &tmTime);

            const int year = tmTime.tm_year + 1900;
            const int month = tmTime.tm_mon + 1;
            const int day = tmTime.tm_mday;
            const int hour = tmTime.tm_hour;
            const int minute = tmTime.tm_min;
            const int second = tmTime.tm_sec;
            const long microsecond = us.count();
            snprintf(buf, sizeof(buf),
                     "Dropped log messages at %04d-%02d-%02d %02d:%02d:%02d.%6ld, %zd larger buffers\n",
                     year, month, day, hour, minute, second, microsecond,
                     buffersToWrite.size() - 2);
            fputs(buf, stderr);
            logFile.append(buf, strlen(buf));
            buffersToWrite.erase(buffersToWrite.begin() + 2, buffersToWrite.end());
            // TODO: 其他处理日志堆积的措施
        }

        // 将待同步到磁盘的缓冲区都追加到文件中，即写入到磁盘
        for (const auto &buffer: buffersToWrite) {
            logFile.append(buffer->data(), buffer->len());
        }

        // 多余的缓冲区直接丢掉，只留下2个留作复用即可
        if (buffersToWrite.size() > 2) {
            buffersToWrite.resize(2);
        }

        // 将已经同步到磁盘的缓冲区再拿出来清空复用
        if (!newBuffer1) {
            assert(!buffersToWrite.empty());
            newBuffer1 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer1->clear();
        }

        if (!newBuffer2) {
            assert(!buffersToWrite.empty());
            newBuffer2 = std::move(buffersToWrite.back());
            buffersToWrite.pop_back();
            newBuffer2->clear();
        }

        // 清空 buffersToWrite 并 flush 文件流
        buffersToWrite.clear();
        logFile.flush();
    }
    logFile.flush();
}

AsyncLogging::AsyncLogging(const int flushInterval) :
        flushInterval_(flushInterval),
        running_(false),
        currentBuffer_(new LogBuffer),
        nextBuffer_(new LogBuffer) {
    currentBuffer_->clear();
    nextBuffer_->clear();
    buffers_.reserve(16);
}

void AsyncLogging::append(const char *logLine, const size_t len) {
    std::lock_guard lock(mutex_);
    if (currentBuffer_->writableBytes() > len) {
        // 如果当前工作缓冲区还没有写满，则直接追加到当前工作缓冲区
        currentBuffer_->append(logLine, len);
    } else {
        // 否则将当前工作缓冲区送入 buffers，以待同步到磁盘
        buffers_.push_back(std::move(currentBuffer_));
        // 尝试将预备缓冲区用作当前工作缓冲区
        if (nextBuffer_) {
            // 如果存在空闲的预备缓冲区，则将该预备缓冲区用作当前工作缓冲区
            currentBuffer_ = std::move(nextBuffer_);
        } else {
            // 如果前端写日志的速率太快，导致预备缓冲区也被用完了，则新分配一块缓冲区作为当前工作缓冲区（实际应用中极少出现此种情况）
            currentBuffer_ = std::make_unique<LogBuffer>();
        }
        // 将日志追加到新设置的当前工作缓冲区
        currentBuffer_->append(logLine, len);
        // 唤醒后端线程将 buffers 中的缓冲区同步到磁盘
        cvBuffersEmpty_.notify_one();
    }
}
