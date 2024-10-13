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

#ifndef HTTPREQUEST_HPP
#define HTTPREQUEST_HPP

#include <string>
#include <unordered_map>
#include <unordered_set>

#include "../buffer/Buffer.hpp"

class HttpRequest {
    enum class ParseState {
        ParseLine,
        ParseHeaders,
        ParseContent,
        ParseFinish
    };

    enum class Method {
        Get,
        Post,
        Head,
        Put,
        Delete,
        Connect,
        Options,
        Trace
    };

    enum class Version {
        Unknown,
        Http10,
        Http11
    };

    ParseState state_;
    Method method_;
    Version version_;
    std::string path_, query_, content_;
    std::unordered_map<std::string, std::string> headers_;
    std::unordered_map<std::string, std::string> posts_;

    inline static const std::unordered_map<Method, std::string> METHOD_STR = {
        {Method::Get, "GET"},
        {Method::Post, "POST"},
        {Method::Head, "HEAD"},
        {Method::Put, "PUT"},
        {Method::Delete, "DELETE"},
        {Method::Connect, "CONNECT"},
        {Method::Options, "OPTIONS"},
        {Method::Trace, "TRACE"}
    };

    inline static const std::unordered_map<Version, std::string> VERSION_STR = {
        {Version::Unknown, "HTTP/Unknown"},
        {Version::Http10, "HTTP/1.0"},
        {Version::Http11, "HTTP/1.1"}
    };

    inline static std::unordered_set<std::string> ALL_HTML;

    // 处理请求行
    bool parseRequestLine(const std::string &line);

    // 处理请求方法
    bool parseMethod(const std::string &line);

    // 处理请求头
    bool parseHeaders(const std::string &line);

    // 处理请求正文
    void parseContent(const std::string &line);

    // 处理Post事件
    void parsePost();

    // 从url中解析编码
    void parseFromUrlEncoded();

    // 用户验证
    [[nodiscard]]
    static bool userVerify(const std::string &name, const std::string &pwd, bool isLogin);

    // 单个16进制字符转换为10进制整数
    [[nodiscard]]
    static int hexCovert(char ch);

public:
    HttpRequest(): state_(ParseState::ParseLine), method_(Method::Get), version_(Version::Http11) {
    }

    ~HttpRequest() = default;

    void clear();

    bool parse(Buffer &buff);

    [[nodiscard]]
    std::string path() const;

    [[nodiscard]]
    std::string &path();

    [[nodiscard]]
    std::string method() const;

    [[nodiscard]]
    std::string version() const;

    [[nodiscard]]
    std::string getPost(const std::string &key) const;

    [[nodiscard]]
    std::string getPost(const char *key) const;

    [[nodiscard]]
    bool isKeepAlive() const;

    static void preloadAllHtml(const std::string &rootPath, bool recursive = false);
};


#endif //HTTPREQUEST_HPP
