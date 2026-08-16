#include <gtest/gtest.h>

#include <string>

#include "http/HttpRequest.hpp"

namespace {

// Buffer 不可拷贝：直接对实例填充
void fillBuff(Buffer& buff, const std::string& data) {
    buff.append(data);
}

// 完整请求一次送入
ParseResult parseOnce(HttpRequest& req, const std::string& raw) {
    Buffer buff;
    fillBuff(buff, raw);
    return req.parse(buff);
}

} // namespace

TEST(HttpRequestTest, ParseSimpleGet) {
    HttpRequest req;
    const auto result = parseOnce(req,
                                  "GET /index.html HTTP/1.1\r\n"
                                  "Host: example.com\r\n"
                                  "\r\n");
    EXPECT_EQ(result, ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/index.html");
    EXPECT_EQ(req.getMethod(), "GET");
    EXPECT_EQ(req.getVersion(), "HTTP/1.1");
}

TEST(HttpRequestTest, RootMapsToIndexHtml) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/index.html");
}

TEST(HttpRequestTest, QuerySplit) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET /path?a=1&b=2 HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/path");
}

TEST(HttpRequestTest, PostWithBody) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req,
                        "POST /login.html HTTP/1.1\r\n"
                        "Host: x\r\n"
                        "Content-Type: application/x-www-form-urlencoded\r\n"
                        "Content-Length: 11\r\n"
                        "\r\n"
                        "a=1&b=hello"),
              ParseResult::Ok);
    EXPECT_EQ(req.getMethod(), "POST");
    EXPECT_EQ(req.getPost("a"), "1");
    EXPECT_EQ(req.getPost("b"), "hello");
}

TEST(HttpRequestTest, IncrementalNeedMore) {
    HttpRequest req;
    Buffer buff;
    // 第一段：只有部分请求行
    buff.append("GET /index.ht");
    EXPECT_EQ(req.parse(buff), ParseResult::NeedMore);
    // 第二段：补齐请求行 + 头部
    buff.append("ml HTTP/1.1\r\nHost: x\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::NeedMore);
    // 第三段：空行结束头部
    buff.append("\r\n");
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/index.html");
}

TEST(HttpRequestTest, BodyArrivesInPieces) {
    HttpRequest req;
    Buffer buff;
    buff.append("POST /login.html HTTP/1.1\r\nHost: x\r\nContent-Length: 10\r\n\r\nab");
    EXPECT_EQ(req.parse(buff), ParseResult::NeedMore);
    buff.append("cdefghij");
    EXPECT_EQ(req.parse(buff), ParseResult::Ok);
}

TEST(HttpRequestTest, MalformedRequestLine) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GARBAGE\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 405); // 未知方法 -> Method Not Allowed
}

TEST(HttpRequestTest, BadVersion) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/9.9\r\nHost: x\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(HttpRequestTest, MissingColonHeader) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost x\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(HttpRequestTest, UnsupportedMethod) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "PUT /index.html HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 405);
}

TEST(HttpRequestTest, ChunkedRejected) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "POST /login.html HTTP/1.1\r\nHost: x\r\nTransfer-Encoding: chunked\r\n\r\n0\r\n\r\n"),
              ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 501);
}

TEST(HttpRequestTest, InvalidContentLength) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "POST /login.html HTTP/1.1\r\nHost: x\r\nContent-Length: abc\r\n\r\n"),
              ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(HttpRequestTest, ConflictingContentLength) {
    HttpRequest req;
    // 重复 Content-Length 且不一致 -> 400（防请求走私）
    EXPECT_EQ(parseOnce(req, "POST /login.html HTTP/1.1\r\nHost: x\r\nContent-Length: 5\r\nContent-Length: 6\r\n\r\n"),
              ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(HttpRequestTest, BodyTooLarge) {
    HttpRequest req;
    const size_t saved = HttpRequest::maxBodySize;
    HttpRequest::maxBodySize = 16;
    EXPECT_EQ(parseOnce(req, "POST /login.html HTTP/1.1\r\nHost: x\r\nContent-Length: 100\r\n\r\n"),
              ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 413);
    HttpRequest::maxBodySize = saved;
}

TEST(HttpRequestTest, UriTooLong) {
    HttpRequest req;
    const size_t saved = HttpRequest::maxUriLength;
    HttpRequest::maxUriLength = 32;
    EXPECT_EQ(parseOnce(req, "GET /" + std::string(64, 'a') + " HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 414);
    HttpRequest::maxUriLength = saved;
}

TEST(HttpRequestTest, TooManyHeaders) {
    HttpRequest req;
    const size_t saved = HttpRequest::maxHeaderCount;
    HttpRequest::maxHeaderCount = 5;
    std::string raw = "GET / HTTP/1.1\r\nHost: x\r\n";
    for (int i = 0; i < 10; ++i) {
        raw += "X-Hdr-" + std::to_string(i) + ": v\r\n";
    }
    raw += "\r\n";
    EXPECT_EQ(parseOnce(req, raw), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 413);
    HttpRequest::maxHeaderCount = saved;
}

TEST(HttpRequestTest, HeaderLineTooLong) {
    HttpRequest req;
    const size_t saved = HttpRequest::maxHeaderLine;
    HttpRequest::maxHeaderLine = 64;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: " + std::string(128, 'x') + "\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 413);
    HttpRequest::maxHeaderLine = saved;
}

TEST(HttpRequestTest, CrlfInjectionRejected) {
    HttpRequest req;
    // 请求行内嵌 CRLF -> 拒绝（防响应拆分/日志注入）
    EXPECT_EQ(parseOnce(req, "GET /a\r\nX-Evil: 1 HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Error);
    EXPECT_EQ(req.statusCode(), 400);
}

TEST(HttpRequestTest, HeaderNamesCaseInsensitive) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHoSt: x\r\nCoNnEcTiOn: keep-alive\r\n\r\n"), ParseResult::Ok);
    EXPECT_TRUE(req.isKeepAlive());
}

TEST(HttpRequestTest, KeepAliveDefaultHttp11) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    EXPECT_TRUE(req.isKeepAlive()); // HTTP/1.1 默认 keep-alive
}

TEST(HttpRequestTest, KeepAliveExplicitClose) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: x\r\nConnection: close\r\n\r\n"), ParseResult::Ok);
    EXPECT_FALSE(req.isKeepAlive());
}

TEST(HttpRequestTest, KeepAliveHttp10) {
    HttpRequest req;
    // HTTP/1.0 默认关闭
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.0\r\n\r\n"), ParseResult::Ok);
    EXPECT_FALSE(req.isKeepAlive());
    // 显式 keep-alive
    HttpRequest req2;
    EXPECT_EQ(parseOnce(req2, "GET / HTTP/1.0\r\nConnection: keep-alive\r\n\r\n"), ParseResult::Ok);
    EXPECT_TRUE(req2.isKeepAlive());
}

TEST(HttpRequestTest, KeepAliveValueCaseInsensitive) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: x\r\nConnection: Keep-Alive\r\n\r\n"), ParseResult::Ok);
    EXPECT_TRUE(req.isKeepAlive());
}

TEST(HttpRequestTest, ConnectionTokenList) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET / HTTP/1.1\r\nHost: x\r\nConnection: keep-alive, Upgrade\r\n\r\n"), ParseResult::Ok);
    EXPECT_TRUE(req.isKeepAlive());
}

TEST(HttpRequestTest, ClearResetsState) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "GET /index.html HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    req.clear();
    EXPECT_EQ(parseOnce(req, "GET /other.html HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    EXPECT_EQ(req.getPath(), "/other.html");
    EXPECT_EQ(req.getPost("a"), "");
}

TEST(HttpRequestTest, EmptyBufferNeedMore) {
    HttpRequest req;
    Buffer buff;
    EXPECT_EQ(req.parse(buff), ParseResult::NeedMore);
}

TEST(HttpRequestTest, HeadMethod) {
    HttpRequest req;
    EXPECT_EQ(parseOnce(req, "HEAD /index.html HTTP/1.1\r\nHost: x\r\n\r\n"), ParseResult::Ok);
    EXPECT_EQ(req.getMethod(), "HEAD");
    EXPECT_EQ(req.getMethodEnum(), HttpRequest::Method::Head);
}

TEST(HttpRequestTest, SanitizePathTraversalRejected) {
    std::string out;
    EXPECT_FALSE(HttpRequest::sanitizePath("/../etc/passwd", out));
    EXPECT_FALSE(HttpRequest::sanitizePath("/a/../../etc/passwd", out));
    EXPECT_FALSE(HttpRequest::sanitizePath("..", out));
    EXPECT_FALSE(HttpRequest::sanitizePath("", out));
    EXPECT_FALSE(HttpRequest::sanitizePath("relative/path", out));
}

TEST(HttpRequestTest, SanitizePathNormalizes) {
    std::string out;
    EXPECT_TRUE(HttpRequest::sanitizePath("/", out));
    EXPECT_EQ(out, "/index.html");
    EXPECT_TRUE(HttpRequest::sanitizePath("/a//b/./c", out));
    EXPECT_EQ(out, "/a/b/c");
    EXPECT_TRUE(HttpRequest::sanitizePath("/dir/", out));
    EXPECT_EQ(out, "/dir/index.html");
}
