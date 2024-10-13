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

#include "LogFile.hpp"

#include <cstring>

void LogFile::AppendFile::append(const char *data, const size_t size) {
    size_t n = write(data, size);
    size_t remain = size - n;
    while (remain > 0) {
        const size_t x = write(data + n, remain);
        if (x == 0) {
            if (ferror(fp_)) {
                fprintf(stderr, "Error writing to file: %s\n", strerror(errno));
            }
            break;
        }
        n += x;
        remain = size - n;
    }
    writtenBytes_ += size;
}

void LogFile::appendUnlocked(const char *data, const size_t size) {
    file_->append(data, size);
    if (file_->writtenBytes() > rollSize_) {
        rollFile();
    } else {
        ++count_;
        if (count_ >= checkEveryN_) {
            count_ = 0;
            const time_t now = time(nullptr);
            // 当天0点的时间戳
            if (const time_t thisPeriod = now / rollPerSeconds_ * rollPerSeconds_; thisPeriod != startOfPeriod_) {
                rollFile();
            } else if (now - lastFlush_ > flushInterval_) {
                lastFlush_ = now;
                file_->flush();
            }
        }
    }
}

std::string LogFile::getLogFileName(const std::string &baseName, time_t &now) {
    std::string filename;
    filename.reserve(baseName.size() + 64);
    filename = baseName;

    char timeBuf[32];
    tm timeInfo{};
    now = time(nullptr);
    localtime_r(&now, &timeInfo);
    strftime(timeBuf, sizeof(timeBuf), ".%Y%m%d-%H%M%S.", &timeInfo);
    filename += timeBuf;

    char pidBuf[32];
    snprintf(pidBuf, sizeof(pidBuf), "%d", getpid());
    filename += pidBuf;

    filename += ".log";

    return filename;
}
