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

#ifndef TINYWEBSERVER_HTTPREQUEST_HPP
#define TINYWEBSERVER_HTTPREQUEST_HPP

#include <cstdint>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../log/Logger.hpp"
#include "../net/Buffer.hpp"

// 增量解析结果：
//   Ok       - 一个完整请求已解析完成（可生成响应）
//   NeedMore - 当前缓冲数据不完整，等待更多数据到达（绝不能误报 400）
//   Error    - 请求非法，通过 statusCode() 获取应返回的状态码（400/405/413/414/501）
enum class ParseResult : std::uint8_t { Ok, NeedMore, Error };

class HttpRequest {
    enum class ParseState : std::uint8_t { ParseLine, ParseHeaders, ParseContent, ParseFinish };

public:
    // 服务器支持的方法白名单；其余方法显式返回 405，绝不按 GET 处理
    enum class Method : std::uint8_t { Get, Post, Head };

    enum class Version : std::uint8_t { Unknown, Http10, Http11 };

private:
    ParseState state_ = ParseState::ParseLine;
    Method method_ = Method::Get;
    Version version_ = Version::Http11;
    // path_ / query_ / content_ 均已做 URL 解码
    std::string path_, query_, content_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> posts_;
    // 解析失败时应返回的 HTTP 状态码
    int statusCode_ = 200;

    // 待读取的请求体字节数（由 Content-Length 决定）
    size_t contentLength_ = 0;

    inline static const std::unordered_map<Method, std::string> METHOD_STR = {
        {Method::Get, "GET"}, {Method::Post, "POST"}, {Method::Head, "HEAD"}};

    inline static const std::unordered_map<Version, std::string> VERSION_STR = {
        {Version::Unknown, "HTTP/Unknown"}, {Version::Http10, "HTTP/1.0"}, {Version::Http11, "HTTP/1.1"}};

    inline static std::unordered_set<std::string> ALL_HTML;

    // 解析请求行（method + uri + version），uri 在本函数内做 URL 解码与路径规范化
    ParseResult parseRequestLine(const std::string& line);

    bool parseMethod(const std::string& line);

    // 解析单个 Header 行；header 名统一转小写存储（RFC 7230 大小写不敏感）
    ParseResult parseHeaders(const std::string& line);

    // 请求体已完整到达后调用；解析表单参数并处理登录/注册
    void parsePost();

    void parseFromUrlEncoded();

    // 用户验证（登录/注册）
    [[nodiscard]]
    static bool userVerify(const std::string& name, const std::string& pwd, bool isLogin);

    // 单个 16 进制字符转换为整数；非法字符返回 -1
    [[nodiscard]]
    static int hexCovert(char ch);

    // 对 percent-encoding（%XX）与 '+'（仅 query）做 URL 解码；
    // 输入含非法编码或 %00 时返回 false
    static bool urlDecode(const std::string& input, bool plusAsSpace, std::string& output);

    // 解析并校验 Content-Length；重复/非法/超限时返回 false
    bool validateContentLength();

    [[nodiscard]]
    bool isChunked() const;

public:
    HttpRequest() = default;

    // 配置化请求限制（单位：字节）。WebServer 启动时可按配置覆盖。
    inline static size_t maxUriLength = 8192;                                   // 请求行总长度上限
    inline static size_t maxHeaderLine = 8192;                                  // 单行 Header 上限
    inline static size_t maxHeaderCount = 100;                                  // Header 数量上限
    inline static size_t maxBodySize = static_cast<size_t>(1024) * 1024;        // 请求体上限（1 MiB）
    inline static size_t maxRequestSize = static_cast<size_t>(8) * 1024 * 1024; // 单连接读缓冲上限（8 MiB）

    // 重置解析状态，供 keep-alive 连接复用
    void clear();

    // 增量解析；绝不因数据不完整而报错
    ParseResult parse(Buffer& buff);

    static void preloadAllHtml(const std::string& rootPath, bool recursive = false);

    // 目录遍历防御（纯字符串级，可单测）：
    // 解码后的原始路径 -> 规范化相对路径。拒绝绝对路径、越界 ..、空段；
    // '/' 映射为 /index.html；无扩展名且命中预加载 HTML 列表时补 .html。
    // 返回 false 表示路径非法（应返回 403/400）。
    [[nodiscard]]
    static bool sanitizePath(const std::string& rawPath, std::string& outPath);

    [[nodiscard]]
    std::string getPath() const {
        return path_;
    }

    [[nodiscard]]
    std::string& getPath() {
        return path_;
    }

    [[nodiscard]]
    std::string getMethod() const {
        return METHOD_STR.at(method_);
    }

    [[nodiscard]]
    Method getMethodEnum() const {
        return method_;
    }

    [[nodiscard]]
    std::string getVersion() const {
        return VERSION_STR.at(version_);
    }

    [[nodiscard]]
    int statusCode() const {
        return statusCode_;
    }

    [[nodiscard]]
    std::string getPost(const std::string& key) const {
        if (const auto it = posts_.find(key); it != posts_.end()) {
            return it->second;
        }
        return "";
    }

    [[nodiscard]]
    std::string getPost(const char* key) const {
        return getPost(std::string(key));
    }

    // RFC 7230：HTTP/1.1 默认 keep-alive，除非显式 Connection: close；
    // HTTP/1.0 默认关闭，除非显式 Connection: keep-alive。值忽略大小写/空白。
    [[nodiscard]]
    bool isKeepAlive() const;
};

#endif // TINYWEBSERVER_HTTPREQUEST_HPP
