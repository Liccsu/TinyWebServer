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

#ifndef TINYWEBSERVER_LOGGER_HPP
#define TINYWEBSERVER_LOGGER_HPP

#include <array>
#include <atomic>
#include <cstdint>
#include <cstdio>
#include <functional>
#include <mutex>
#include <string>
#include <sys/syscall.h>
#include <thread>
#include <unistd.h>
#include <unordered_map>

#include "LogStream.hpp"

// 部分编译器不支持 __FILE_NAME__ 宏
#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

enum class LogLevel : std::uint8_t { Default, Debug, Info, Warning, Error, None };

class Logger {
    using OutputCallback = std::function<void(const char*, size_t)>;
    using FlushCallback = std::function<void()>;

    class Impl {
        inline static thread_local long tid_;
        inline static thread_local std::array<char, 8> tidStr_;
        const char* fileName_;
        const int line_;
        const LogLevel logLevel_;
        LogStream logStream_;

        inline static const std::unordered_map<LogLevel, std::string> LevelColor = {{LogLevel::Default, "\033[0m"},
                                                                                    {LogLevel::Debug, "\033[34m"},
                                                                                    {LogLevel::Info, "\033[32m"},
                                                                                    {LogLevel::Warning, "\033[33m"},
                                                                                    {LogLevel::Error, "\033[31m"}};

    public:
        explicit Impl(const char* fileName, const int line, const LogLevel logLevel)
            : fileName_(fileName),
              line_(line),
              logLevel_(logLevel) {
            if (Logger::colorfulEnabled()) {
                logStream_ << LevelColor.at(logLevel_);
            }
            formatNowTime();
            if (tid_ == 0) {
                tid_ = syscall(SYS_gettid);
                std::snprintf(tidStr_.data(), tidStr_.size(), "%5ld", tid_);
            }
            logStream_ << ' ' << tidStr_.data() << ' ' << logLevelTag() << ": ";
        }

        ~Impl() {
            logStream_ << " - " << fileName_ << ':' << line_;
            if (Logger::colorfulEnabled()) {
                logStream_ << "\033[0m";
            }
            logStream_ << '\n';
            // 在锁内拷贝回调，锁外调用：回调（AsyncLogging::append）可能持有自己的锁，
            // 避免与 setOutPutCallback 的写锁产生锁序问题
            const auto outCb = Logger::outputCallback();
            outCb(logStream_.buffer().data(), logStream_.buffer().len());
            if (logLevel_ == LogLevel::Error) {
                const auto flushCb = Logger::flushCallback();
                flushCb();
            }
        }

        [[nodiscard]]
        LogStream& logStream() {
            return logStream_;
        }

        void formatNowTime();

        [[nodiscard]]
        char logLevelTag() const;
    };

    Impl impl_;
    // 日志最低输出级别（多线程安全）
    inline static std::atomic<LogLevel> lowestLevel_{LogLevel::Info};
    // 是否启用终端彩色日志输出（多线程安全）
    inline static std::atomic<bool> colorful_{false};

    // 输出/冲刷回调由 static mutex 保护，支持运行期切换（如退出时解绑，防止悬垂捕获）
    inline static std::mutex callbackMutex_;
    inline static OutputCallback outPutCallback_ = [](const char* const buffer, const size_t len) {
        fwrite(buffer, len, sizeof(char), stdout);
    };

    inline static FlushCallback flushCallback_ = [] { fflush(stdout); };

public:
    // 此对象不应该被复制或移动，它应该仅仅是随用随弃的临时对象，以充分利用RAII机制
    Logger(const Logger&) = delete;

    Logger(Logger&&) = delete;

    Logger& operator=(const Logger&) = delete;

    Logger& operator=(Logger&&) = delete;

    Logger(const char* fileName, const int line, const LogLevel logLevel)
        : impl_(fileName, line, logLevel) {}

    [[nodiscard]]
    LogStream& logStream() {
        return impl_.logStream();
    }

    [[nodiscard]]
    static OutputCallback outputCallback() {
        std::lock_guard lock(callbackMutex_);
        return outPutCallback_;
    }

    [[nodiscard]]
    static FlushCallback flushCallback() {
        std::lock_guard lock(callbackMutex_);
        return flushCallback_;
    }

    static void setLogLevel(const LogLevel logLevel) { lowestLevel_.store(logLevel, std::memory_order_release); }

    [[nodiscard]]
    static LogLevel logLevel() {
        return lowestLevel_.load(std::memory_order_acquire);
    }

    static void setOutPutCallback(const OutputCallback& outPutCallback) {
        std::lock_guard lock(callbackMutex_);
        outPutCallback_ = outPutCallback;
    }

    static void setOutPutCallback(OutputCallback&& outPutCallback) {
        std::lock_guard lock(callbackMutex_);
        outPutCallback_ = std::move(outPutCallback);
    }

    // 恢复默认 stdout 输出（析构/退出时调用，解除对已析构对象的捕获）
    static void resetOutPutCallback() {
        std::lock_guard lock(callbackMutex_);
        outPutCallback_ = [](const char* const buffer, const size_t len) { fwrite(buffer, len, sizeof(char), stdout); };
    }

    static void setFlushCallback(const FlushCallback& flushCallback) {
        std::lock_guard lock(callbackMutex_);
        flushCallback_ = flushCallback;
    }

    static void setFlushCallback(FlushCallback&& flushCallback) {
        std::lock_guard lock(callbackMutex_);
        flushCallback_ = std::move(flushCallback);
    }

    static void enableColorful() { colorful_.store(true, std::memory_order_release); }

    static void disableColorful() { colorful_.store(false, std::memory_order_release); }

    [[nodiscard]]
    static bool colorfulEnabled() {
        return colorful_.load(std::memory_order_acquire);
    }
};

#define LOGD                                                                                                           \
    if (Logger::logLevel() <= LogLevel::Debug)                                                                         \
    Logger(__FILE_NAME__, __LINE__, LogLevel::Debug).logStream()

#define LOGI                                                                                                           \
    if (Logger::logLevel() <= LogLevel::Info)                                                                          \
    Logger(__FILE_NAME__, __LINE__, LogLevel::Info).logStream()

#define LOGW                                                                                                           \
    if (Logger::logLevel() <= LogLevel::Warning)                                                                       \
    Logger(__FILE_NAME__, __LINE__, LogLevel::Warning).logStream()

#define LOGE                                                                                                           \
    if (Logger::logLevel() <= LogLevel::Error)                                                                         \
    Logger(__FILE_NAME__, __LINE__, LogLevel::Error).logStream()

#endif // TINYWEBSERVER_LOGGER_HPP
