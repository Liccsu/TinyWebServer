#include <gtest/gtest.h>

#include <string>

#include "net/Buffer.hpp"

namespace {} // namespace

TEST(BufferTest, InitialState) {
    Buffer buff;
    EXPECT_EQ(buff.readableSize(), 0u);
    EXPECT_GT(buff.writableSize(), 0u);
    EXPECT_EQ(buff.peek(), buff.beginWrite());
}

TEST(BufferTest, AppendAndRead) {
    Buffer buff;
    buff.append("hello");
    EXPECT_EQ(buff.readableSize(), 5u);
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), "hello");

    buff.retrieve(2);
    EXPECT_EQ(buff.readableSize(), 3u);
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), "llo");

    buff.append("world");
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), "lloworld");
}

TEST(BufferTest, AppendBeyondCapacityExtends) {
    Buffer buff(16);
    const std::string big(4096, 'x');
    buff.append(big);
    EXPECT_EQ(buff.readableSize(), big.size());
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), big);
}

TEST(BufferTest, RetrieveUntil) {
    Buffer buff;
    buff.append("GET / HTTP/1.1\r\nHost: x\r\n\r\n");
    const char* lineEnd = buff.peek() + 14; // "GET / HTTP/1.1"
    buff.retrieveUntil(lineEnd);
    EXPECT_EQ(buff.readableSize(), 13u); // 剩余 "\r\nHost: x\r\n\r\n"（27-14）
}

TEST(BufferTest, RetrieveAllResets) {
    Buffer buff;
    buff.append("abc");
    buff.retrieveAll();
    EXPECT_EQ(buff.readableSize(), 0u);
    // 复用后仍可正常写入
    buff.append("xyz");
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), "xyz");
}

TEST(BufferTest, AppendBuffer) {
    Buffer a;
    a.append("abc");
    Buffer b;
    b.append("def");
    a.append(b);
    EXPECT_EQ(std::string(a.peek(), a.readableSize()), "abcdef");
}

TEST(BufferTest, ReadFdFromPipe) {
    // 用管道模拟 socket fd 的 readv 路径（payload 小于管道缓冲，避免写阻塞）
    int fds[2];
    ASSERT_EQ(pipe(fds), 0);
    const std::string payload(30000, 'q');
    ASSERT_EQ(write(fds[1], payload.data(), payload.size()), static_cast<ssize_t>(payload.size()));

    Buffer buff;
    auto [len, err] = buff.readFd(fds[0]);
    EXPECT_GT(len, 0);
    EXPECT_EQ(err, 0);
    EXPECT_EQ(buff.readableSize(), static_cast<size_t>(len));
    EXPECT_EQ(std::string(buff.peek(), buff.readableSize()), payload);

    close(fds[1]);
    close(fds[0]);
}

TEST(BufferTest, ReadFdClosedFdReturnsError) {
    Buffer buff;
    auto [len, err] = buff.readFd(-1);
    EXPECT_LT(len, 0);
    EXPECT_NE(err, 0);
}
