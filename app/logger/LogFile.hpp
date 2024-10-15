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

#ifndef TINYWEBSERVER_LOGFILE_HPP
#define TINYWEBSERVER_LOGFILE_HPP

#include <cassert>
#include <ctime>
#include <iostream>
#include <memory>
#include <mutex>
#include <string>

#include "../config/Config.hpp"

class LogFile {
    class AppendFile {
        FILE *fp_;
        char buffer_[64 * 1024]{};
        size_t writtenBytes_{};

        size_t write(const char *data, const size_t size) const {
            return fwrite_unlocked(data, 1, size, fp_);
        }

    public:
        explicit AppendFile(const std::string &fileName) :
                fp_(fopen(fileName.c_str(), "ae")) {
            assert(fp_);
            setbuffer(fp_, buffer_, sizeof(buffer_));
        }

        ~AppendFile() {
            fclose(fp_);
        }

        void append(const char *data, size_t size);

        void flush() const {
            fflush(fp_);
        }

        [[nodiscard]]
        size_t writtenBytes() const {
            return writtenBytes_;
        }
    };

    const std::string baseName_;
    const size_t rollSize_;
    const int flushInterval_;
    const int checkEveryN_;

    int count_{};

    std::mutex mutex_;
    time_t startOfPeriod_{};
    time_t lastRoll_{};
    time_t lastFlush_{};
    std::unique_ptr<AppendFile> file_;

    static constexpr int rollPerSeconds_ = 60 * 60 * 24;

    void appendUnlocked(const char *data, size_t size);

    static std::string getLogFileName(const std::string &baseName, time_t &now);

public:
    explicit LogFile(const int flushInterval = 3, const int checkEveryN = 1024) :
            baseName_(Config::get<std::string>("log.basename")),
            rollSize_(Config::get<size_t>("log.size") * 1024 * 1024),
            flushInterval_(flushInterval),
            checkEveryN_(checkEveryN) {
        assert(baseName_.find('/') == std::string::npos);
        rollFile();
    }

    ~LogFile() = default;

    void append(const char *data, const size_t size) {
        std::lock_guard lock(mutex_);
        appendUnlocked(data, size);
    }

    void flush() {
        std::lock_guard lock(mutex_);
        file_->flush();
    }

    bool rollFile() {
        time_t now{};
        const std::string fileName = getLogFileName(baseName_, now);
        // 当天开始时的时间戳，即0点
        const time_t start = now / rollPerSeconds_ * rollPerSeconds_;

        if (now > lastRoll_) {
            lastRoll_ = now;
            lastFlush_ = now;
            startOfPeriod_ = start;
            const auto dir = Config::get<std::string>("log.directory");
            std::filesystem::path path(dir);
            path.append(fileName);
            const std::filesystem::path file = absolute(path);
            // 如果目录不存在则创建
            if (const auto dir_path = file.parent_path(); !exists(dir_path)) {
                create_directories(dir_path);
            }
            file_ = std::make_unique<AppendFile>(file.string());
            return true;
        }
        return false;
    }
};


#endif //TINYWEBSERVER_LOGFILE_HPP
