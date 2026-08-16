#include <gtest/gtest.h>

#include <cstdio>
#include <filesystem>
#include <fstream>
#include <string>
#include <system_error>

#include "http/HttpResponse.hpp"

namespace {

namespace fs = std::filesystem;

class HttpResponseTest : public ::testing::Test {
protected:
    fs::path root_;

    void SetUp() override {
        root_ = fs::temp_directory_path() / ("tws_test_dist_" + std::to_string(::getpid()));
        fs::create_directories(root_ / "sub");
        writeFile("index.html", "<html>index</html>");
        writeFile("sub/page.html", "<html>page</html>");
        writeFile("secret.txt", "TOP SECRET");
        writeFile("empty.bin", "");
        writeFile("big.bin", std::string(100000, 'x'));
    }

    void TearDown() override {
        std::error_code ec;
        fs::remove_all(root_, ec);
    }

    void writeFile(const std::string& rel, const std::string& content) {
        std::ofstream f(root_ / rel, std::ios::binary);
        f << content;
    }

    // 生成响应并返回响应缓冲
    std::string makeResponse(
        HttpResponse& resp, const std::string& path, bool keepAlive = false, int code = 200, bool isHead = false) {
        Buffer buff;
        resp.init(root_.string(), path, keepAlive, code, isHead);
        resp.makeResponse(buff);
        return std::string(buff.peek(), buff.readableSize());
    }
};

} // namespace

TEST_F(HttpResponseTest, ServeExistingFile) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/index.html");
    EXPECT_EQ(resp.code(), 200);
    EXPECT_NE(raw.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(raw.find("Content-length: "), std::string::npos);
    // 文件内容经 mmap 提供（iov[1]），不写入响应 Buffer
    EXPECT_EQ(resp.fileLen(), 18u); // "<html>index</html>" 长度
    EXPECT_NE(resp.file(), nullptr);
    EXPECT_EQ(std::string(resp.file(), resp.fileLen()), "<html>index</html>");
    resp.unmapFile();
}

TEST_F(HttpResponseTest, MissingFile404) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/nope.html");
    EXPECT_EQ(resp.code(), 404);
    EXPECT_NE(raw.find("HTTP/1.1 404 Not Found"), std::string::npos);
}

TEST_F(HttpResponseTest, Directory404) {
    HttpResponse resp;
    // 目录请求（HttpRequest 会补 index.html，此处直接测防御）
    const std::string raw = makeResponse(resp, "/sub");
    EXPECT_EQ(resp.code(), 404);
    EXPECT_NE(raw.find("404"), std::string::npos);
}

TEST_F(HttpResponseTest, TraversalBlocked403) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/../secret.txt");
    EXPECT_EQ(resp.code(), 403);
    EXPECT_NE(raw.find("403"), std::string::npos);
}

TEST_F(HttpResponseTest, EncodedTraversalPathStillRejected) {
    // 已解码的 .. 路径必须被拦截（双保险：HttpRequest 层 + HttpResponse 层）
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/a/../../secret.txt");
    EXPECT_EQ(resp.code(), 403);
    EXPECT_NE(raw.find("403"), std::string::npos);
}

TEST_F(HttpResponseTest, SymlinkEscapeBlocked) {
    // 符号链接指向文档根之外 -> 403
    const fs::path outside = fs::temp_directory_path() / ("tws_secret_outside_" + std::to_string(::getpid()));
    {
        std::ofstream f(outside);
        f << "outside data";
    }
    std::error_code ec;
    fs::create_symlink(outside, root_ / "escape_link.txt", ec);
    if (!ec) {
        HttpResponse resp;
        const std::string raw = makeResponse(resp, "/escape_link.txt");
        EXPECT_EQ(resp.code(), 403);
        EXPECT_NE(raw.find("403"), std::string::npos);
    } else {
        GTEST_SKIP() << "symlink 权限不可用，跳过";
    }
    fs::remove(outside, ec);
}

TEST_F(HttpResponseTest, SymlinkInsideRootAllowed) {
    std::error_code ec;
    fs::create_symlink(root_ / "secret.txt", root_ / "link_inside.txt", ec);
    if (!ec) {
        HttpResponse resp;
        const std::string raw = makeResponse(resp, "/link_inside.txt");
        EXPECT_EQ(resp.code(), 200);
        EXPECT_EQ(std::string(resp.file(), resp.fileLen()), "TOP SECRET");
        resp.unmapFile();
    } else {
        GTEST_SKIP() << "symlink 权限不可用，跳过";
    }
}

TEST_F(HttpResponseTest, HeadRequestNoMmapNoBody) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/index.html", false, 200, true);
    EXPECT_EQ(resp.code(), 200);
    // HEAD：不 mmap，file() 为空；Content-length 仍正确
    EXPECT_EQ(resp.file(), nullptr);
    EXPECT_EQ(resp.fileLen(), 18u);
    EXPECT_NE(raw.find("Content-length: 18"), std::string::npos);
}

TEST_F(HttpResponseTest, ErrorPageIsBuiltin) {
    // 错误页不依赖磁盘文件：即使 400.html 不存在也有完整响应
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/", false, 404);
    EXPECT_NE(raw.find("HTTP/1.1 404 Not Found"), std::string::npos);
    EXPECT_NE(raw.find("<!DOCTYPE html>"), std::string::npos);
    EXPECT_NE(raw.find("404 Not Found"), std::string::npos);
}

TEST_F(HttpResponseTest, TextResponseHealthCheck) {
    // /healthz：200 纯文本，不访问文件系统
    HttpResponse resp;
    Buffer buff;
    resp.makeTextResponse(buff, "ok\n", true, false);
    const std::string raw(buff.peek(), buff.readableSize());
    EXPECT_NE(raw.find("HTTP/1.1 200 OK"), std::string::npos);
    EXPECT_NE(raw.find("Content-length: 3"), std::string::npos);
    EXPECT_NE(raw.find("ok\n"), std::string::npos);
    EXPECT_EQ(resp.file(), nullptr); // 不 mmap 文件
    // keep-alive 头
    EXPECT_NE(raw.find("Connection: keep-alive"), std::string::npos);
}

TEST_F(HttpResponseTest, TextResponseReusesObjectSafely) {
    // 先服务文件再 healthz：旧 mmap 必须释放
    HttpResponse resp;
    makeResponse(resp, "/index.html");
    ASSERT_NE(resp.file(), nullptr);
    Buffer buff;
    resp.makeTextResponse(buff, "ok\n", false, false);
    EXPECT_EQ(resp.file(), nullptr);
    EXPECT_EQ(resp.code(), 200);
}

TEST_F(HttpResponseTest, MimeTypeByExtension) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/index.html");
    EXPECT_NE(raw.find("Content-type: text/html"), std::string::npos);
    resp.unmapFile();

    HttpResponse resp2;
    const std::string raw2 = makeResponse(resp2, "/sub/page.html");
    EXPECT_NE(raw2.find("Content-type: text/html"), std::string::npos);
    resp2.unmapFile();

    HttpResponse resp3;
    const std::string raw3 = makeResponse(resp3, "/big.bin");
    EXPECT_NE(raw3.find("Content-type: text/plain"), std::string::npos);
    resp3.unmapFile();
}

TEST_F(HttpResponseTest, EmptyFile200) {
    HttpResponse resp;
    const std::string raw = makeResponse(resp, "/empty.bin");
    EXPECT_EQ(resp.code(), 200);
    EXPECT_NE(raw.find("Content-length: 0"), std::string::npos);
}

TEST_F(HttpResponseTest, UnmapFileIsIdempotent) {
    HttpResponse resp;
    makeResponse(resp, "/index.html");
    EXPECT_NE(resp.file(), nullptr);
    resp.unmapFile();
    resp.unmapFile(); // 二次调用安全
    EXPECT_EQ(resp.file(), nullptr);
}

TEST_F(HttpResponseTest, InitReuseUnmapsPrevious) {
    HttpResponse resp;
    makeResponse(resp, "/index.html");
    const char* firstFile = resp.file();
    ASSERT_NE(firstFile, nullptr);
    // 同一 response 对象复用：旧的 mmap 被释放，新文件映射
    const std::string raw = makeResponse(resp, "/big.bin");
    EXPECT_EQ(resp.code(), 200);
    EXPECT_NE(resp.file(), nullptr);
    EXPECT_NE(resp.file(), firstFile);
    resp.unmapFile();
}
