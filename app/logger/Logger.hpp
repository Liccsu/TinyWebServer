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

#ifndef TINYWEBSERVER_LOGGER_HPP
#define TINYWEBSERVER_LOGGER_HPP

#include <functional>
#include <string>
#include <thread>

#include "LogStream.hpp"

// 部分编译器不支持 __FILE_NAME__ 宏
#ifndef __FILE_NAME__
#define __FILE_NAME__ __FILE__
#endif

enum class LogLevel {
    Default,
    Debug,
    Info,
    Warning,
    Error,
    None
};

class Logger {
    using OutputCallback = std::function<void(const char *, size_t)>;
    using FlushCallback = std::function<void()>;

    class Impl {
        inline static thread_local long tid_;
        inline static thread_local char tidStr_[8];
        const char *fileName_;
        const int line_;
        LogLevel logLevel_;
        LogStream logStream_;

        inline static const std::unordered_map<LogLevel, std::string> LevelColor = {
            {LogLevel::Default, "\033[0m"},
            {LogLevel::Debug, "\033[34m"},
            {LogLevel::Info, "\033[32m"},
            {LogLevel::Warning, "\033[33m"},
            {LogLevel::Error, "\033[31m"}
        };

    public:
        explicit Impl(const char *fileName, const int line, const LogLevel logLevel): fileName_(fileName), line_(line),
            logLevel_(logLevel) {
            if (colorful_) {
                logStream_ << LevelColor.at(logLevel_);
            }
            formatNowTime();
            if (tid_ == 0) {
                tid_ = syscall(SYS_gettid);
                std::snprintf(tidStr_, sizeof(tidStr_), "%5ld", tid_);
            }
            logStream_ << ' ' << tidStr_ << ' ' << logLevelTag() << ": ";
        }

        ~Impl() {
            logStream_ << " - " << fileName_ << ':' << line_;
            if (colorful_) {
                logStream_ << "\033[0m";
            }
            logStream_ << '\n';
            outPutCallback_(logStream_.buffer().data(), logStream_.buffer().len());
            if (logLevel_ == LogLevel::Error) {
                flushCallback_();
            }
        }

        [[nodiscard]]
        LogStream &logStream() {
            return logStream_;
        }

        void formatNowTime();

        [[nodiscard]]
        char logLevelTag() const;
    };

    Impl impl_;
    // 日志最低输出级别
    inline static auto lowestLevel_ = LogLevel::Info;
    // 是否启用终端彩色日志输出
    inline static bool colorful_ = false;

    inline static OutputCallback outPutCallback_ =
            [](const char *const buffer, const size_t len) {
        fwrite(buffer, len, sizeof(char), stdout);
    };

    inline static FlushCallback flushCallback_ = [] {
        fflush(stdout);
    };

public:
    // 此对象不应该被复制或移动，它应该仅仅是随用随弃的临时对象，以充分利用RAII机制
    Logger(const Logger &) = delete;

    Logger(Logger &&) = delete;

    Logger &operator=(const Logger &) = delete;

    Logger &operator=(Logger &&) = delete;

    Logger(const char *fileName, const int line, const LogLevel logLevel): impl_(fileName, line, logLevel) {
    }

    [[nodiscard]]
    LogStream &logStream() {
        return impl_.logStream();
    }

    static void setLogLevel(const LogLevel logLevel) {
        lowestLevel_ = logLevel;
    }

    [[nodiscard]]
    static LogLevel logLevel() {
        return lowestLevel_;
    }

    static void setOutPutCallback(const OutputCallback &outPutCallback) {
        outPutCallback_ = outPutCallback;
    }

    static void setOutPutCallback(OutputCallback &&outPutCallback) {
        outPutCallback_ = std::move(outPutCallback);
    }

    static void setFlushCallback(const FlushCallback &flushCallback) {
        flushCallback_ = flushCallback;
    }

    static void setFlushCallback(FlushCallback &&flushCallback) {
        flushCallback_ = std::move(flushCallback);
    }

    static void enableColorful() {
        colorful_ = true;
    }

    static void disableColorful() {
        colorful_ = false;
    }
};

#define LOGD \
    if (Logger::logLevel() <= LogLevel::Debug) \
        Logger(__FILE_NAME__, __LINE__, LogLevel::Debug).logStream()

#define LOGI \
    if (Logger::logLevel() <= LogLevel::Info) \
        Logger(__FILE_NAME__, __LINE__, LogLevel::Info).logStream()

#define LOGW \
    if (Logger::logLevel() <= LogLevel::Warning) \
        Logger(__FILE_NAME__, __LINE__, LogLevel::Warning).logStream()

#define LOGE \
    if (Logger::logLevel() <= LogLevel::Error) \
        Logger(__FILE_NAME__, __LINE__, LogLevel::Error).logStream()


#endif //TINYWEBSERVER_LOGGER_HPP
