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

#include "Logger.hpp"

#include <chrono>
#include <ctime>

void Logger::Impl::formatNowTime() {
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

    logStream_ << static_cast<char>('0' + year / 1000 % 10);
    logStream_ << static_cast<char>('0' + year / 100 % 10);
    logStream_ << static_cast<char>('0' + year / 10 % 10);
    logStream_ << static_cast<char>('0' + year % 10);

    logStream_ << '-';

    logStream_ << static_cast<char>('0' + month / 10 % 10);
    logStream_ << static_cast<char>('0' + month % 10);

    logStream_ << '-';

    logStream_ << static_cast<char>('0' + day / 10 % 10);
    logStream_ << static_cast<char>('0' + day % 10);

    logStream_ << ' ';

    logStream_ << static_cast<char>('0' + hour / 10 % 10);
    logStream_ << static_cast<char>('0' + hour % 10);

    logStream_ << ':';

    logStream_ << static_cast<char>('0' + minute / 10 % 10);
    logStream_ << static_cast<char>('0' + minute % 10);

    logStream_ << ':';

    logStream_ << static_cast<char>('0' + second / 10 % 10);
    logStream_ << static_cast<char>('0' + second % 10);

    logStream_ << '.';

    logStream_ << static_cast<char>('0' + microsecond / 100000 % 10);
    logStream_ << static_cast<char>('0' + microsecond / 10000 % 10);
    logStream_ << static_cast<char>('0' + microsecond / 1000 % 10);
    logStream_ << static_cast<char>('0' + microsecond / 100 % 10);
    logStream_ << static_cast<char>('0' + microsecond / 10 % 10);
    logStream_ << static_cast<char>('0' + microsecond % 10);
}

char Logger::Impl::logLevelTag() const {
    switch (logLevel_) {
        case LogLevel::Debug:
            return 'D';
        case LogLevel::Info:
            return 'I';
        case LogLevel::Warning:
            return 'W';
        case LogLevel::Error:
            return 'E';
        default:
            return ' ';
    }
}
