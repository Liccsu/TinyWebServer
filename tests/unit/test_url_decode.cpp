#include <gtest/gtest.h>

#include <string>

#include "http/HttpRequest.hpp"

// urlDecode 为私有方法，通过 sanitizePath/parse 的公开行为间接测试。
// 解码行为经 parseRequestLine 的 path 解码验证。

namespace {

void fillBuff(Buffer& buff, const std::string& data) {
    buff.append(data);
}

} // namespace

TEST(UrlDecodeTest, PercentDecoding) {
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a%20b%2Fc HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
    // %20 -> 空格，%2F -> '/'
    EXPECT_EQ(req.getPath(), "/a b/c");
}

TEST(UrlDecodeTest, EncodedTraversalBlocked) {
    // %2e%2e = ".."，解码后必须被 sanitizePath 拦截 -> 403
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /%2e%2e/%2e%2e/etc/passwd HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 403);
}

TEST(UrlDecodeTest, EncodedSlashTraversalBlocked) {
    // ..%2f..%2f 编码斜杠变体
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /..%2f..%2fetc%2fpasswd HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 403);
}

TEST(UrlDecodeTest, TrailingPercentRejected) {
    // 回归：原实现 '%' 在串尾越界读（UB）。现在必须返回 400 而非崩溃/越界
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a% HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(UrlDecodeTest, PercentAtEndPairRejected) {
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a%2 HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(UrlDecodeTest, InvalidHexRejected) {
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a%zz HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(UrlDecodeTest, NullByteRejected) {
    // %00 会在 C 字符串/路径中截断 -> 必须拒绝
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a%00b HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(UrlDecodeTest, PlusInQueryIsSpace) {
    HttpRequest req;
    Buffer buff;
    fillBuff(buff,
             "POST /login.html HTTP/1.1\r\nHost: x\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: 13\r\n\r\n"
             "a=hello+world");
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
    EXPECT_EQ(req.getPost("a"), "hello world");
}

TEST(UrlDecodeTest, PlusInPathIsLiteral) {
    // path 中的 '+' 是字面量，不是空格
    HttpRequest req;
    Buffer buff;
    fillBuff(buff, "GET /a+b HTTP/1.1\r\nHost: x\r\n\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/a+b");
}

TEST(UrlDecodeTest, MixedEncodingInBody) {
    HttpRequest req;
    Buffer buff;
    fillBuff(buff,
             "POST /login.html HTTP/1.1\r\nHost: x\r\n"
             "Content-Type: application/x-www-form-urlencoded\r\n"
             "Content-Length: 18\r\n\r\n"
             "name=%E6%B5%8B&v=1"); // UTF-8 多字节解码
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
    EXPECT_EQ(req.getPost("name"), "\xE6\xB5\x8B");
    EXPECT_EQ(req.getPost("v"), "1");
}
