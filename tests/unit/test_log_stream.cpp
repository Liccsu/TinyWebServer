#include <gtest/gtest.h>

#include <climits>
#include <string>

#include "log/LogStream.hpp"

namespace {

// 将 LogStream 缓冲内容提取为字符串
std::string streamToString(LogStream& stream) {
    return {stream.buffer().data(), stream.buffer().len()};
}

} // namespace

TEST(LogStreamTest, PositiveInt) {
    LogStream stream;
    stream << 12345;
    EXPECT_EQ(streamToString(stream), "12345");
}

TEST(LogStreamTest, Zero) {
    LogStream stream;
    stream << 0;
    EXPECT_EQ(streamToString(stream), "0");
}

TEST(LogStreamTest, NegativeInt) {
    // 回归：负数曾导致 number[负数下标] 越界读（UB）
    LogStream stream;
    stream << -42;
    EXPECT_EQ(streamToString(stream), "-42");
}

TEST(LogStreamTest, IntMin) {
    // 回归：INT_MIN 取负溢出 + 负数取模越界
    LogStream stream;
    stream << INT_MIN;
    EXPECT_EQ(streamToString(stream), "-2147483648");
}

TEST(LogStreamTest, Int64Min) {
    LogStream stream;
    stream << INT64_MIN;
    EXPECT_EQ(streamToString(stream), "-9223372036854775808");
}

TEST(LogStreamTest, UnsignedMax) {
    LogStream stream;
    stream << UINT64_MAX;
    EXPECT_EQ(streamToString(stream), "18446744073709551615");
}

TEST(LogStreamTest, HexPointer) {
    LogStream stream;
    int value = 0;
    stream << &value;
    const std::string s = streamToString(stream);
    EXPECT_EQ(s.substr(0, 2), "0x");
    EXPECT_GT(s.size(), 2u);
}

TEST(LogStreamTest, StringAndChar) {
    LogStream stream;
    stream << "hello" << ' ' << std::string("world");
    EXPECT_EQ(streamToString(stream), "hello world");
}

TEST(LogStreamTest, Bool) {
    LogStream stream;
    stream << true << ',' << false;
    EXPECT_EQ(streamToString(stream), "true,false");
}

TEST(LogStreamTest, Float) {
    LogStream stream;
    stream << 3.5f;
    EXPECT_EQ(streamToString(stream), "3.5");
}

TEST(LogStreamTest, MixedSequence) {
    LogStream stream;
    stream << "fd=" << 42 << " ret=" << -1;
    EXPECT_EQ(streamToString(stream), "fd=42 ret=-1");
}
