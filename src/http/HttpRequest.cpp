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

#include "HttpRequest.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>
#include <filesystem>

#include "../storage/SqlConnPool.hpp"

namespace {

// 从缓冲中查找 "\r\n"；返回行尾指针（不含 CRLF），未找到返回 nullptr
const char* findLineEnd(const char* begin, const char* end) {
    for (const char* p = begin; p + 1 < end; ++p) {
        if (p[0] == '\r' && p[1] == '\n') {
            return p;
        }
    }
    return nullptr;
}

} // namespace

ParseResult HttpRequest::parseRequestLine(const std::string& line) {
    // 请求行格式：METHOD SP request-target SP HTTP-version CRLF
    // 示例：GET /path?arg1=1&arg2=2 HTTP/1.1
    // 行内出现 CR/LF（CRLF 注入）时直接拒绝
    if (line.find('\r') != std::string::npos || line.find('\n') != std::string::npos) {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    size_t start = 0;
    // 跳过行首空白（容忍 " GET /x HTTP/1.1"）
    while (start < line.size() && isspace(static_cast<unsigned char>(line[start]))) {
        ++start;
    }
    size_t pos = start;
    while (pos < line.size() && !isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos == start) {
        statusCode_ = 400;
        return ParseResult::Error;
    }
    if (!parseMethod(line.substr(start, pos - start))) {
        // 方法不支持：显式 405，而不是当作静态文件请求
        statusCode_ = 405;
        return ParseResult::Error;
    }

    // 跳过连续空白，取 request-target
    while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    const size_t targetBegin = pos;
    while (pos < line.size() && !isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    if (pos == targetBegin) {
        statusCode_ = 400;
        return ParseResult::Error;
    }
    const std::string rawTarget = line.substr(targetBegin, pos - targetBegin);

    // 取版本号（跳过连续空白后必须是版本串，不允许多余 token）
    while (pos < line.size() && isspace(static_cast<unsigned char>(line[pos]))) {
        ++pos;
    }
    const std::string version = line.substr(pos);
    if (version == VERSION_STR.at(Version::Http10)) {
        version_ = Version::Http10;
    } else if (version == VERSION_STR.at(Version::Http11)) {
        version_ = Version::Http11;
    } else {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    // request-target 必须为 origin-form（以 '/' 开头）；absolute-form 等其他形式拒绝
    if (rawTarget.empty() || rawTarget[0] != '/') {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    // 拆分 path 与 query（'?' 之后为 query）
    std::string rawPath, rawQuery;
    if (const auto queryPos = rawTarget.find('?'); queryPos == std::string::npos) {
        rawPath = rawTarget;
    } else {
        rawPath = rawTarget.substr(0, queryPos);
        rawQuery = rawTarget.substr(queryPos + 1);
    }

    // URL 解码：path 中 '+' 是字面量，query 中 '+' 表示空格
    std::string decodedPath, decodedQuery;
    if (!urlDecode(rawPath, false, decodedPath) || !urlDecode(rawQuery, true, decodedQuery)) {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    // 目录遍历防御：规范化并拒绝非法路径
    if (!sanitizePath(decodedPath, path_)) {
        statusCode_ = 403;
        return ParseResult::Error;
    }
    query_ = decodedQuery;

    return ParseResult::Ok;
}

bool HttpRequest::parseMethod(const std::string& line) {
    if (line == "GET") {
        method_ = Method::Get;
    } else if (line == "POST") {
        method_ = Method::Post;
    } else if (line == "HEAD") {
        method_ = Method::Head;
    } else {
        return false;
    }
    return true;
}

ParseResult HttpRequest::parseHeaders(const std::string& line) {
    // 空行表示 Header 区结束，进入请求体阶段
    if (line.empty()) {
        state_ = ParseState::ParseContent;
        // 解析 Content-Length（POST 请求体长度校验）
        if (!validateContentLength()) {
            return ParseResult::Error;
        }
        // 显式检测 chunked：本服务器不支持，安全拒绝
        if (isChunked()) {
            statusCode_ = 501;
            return ParseResult::Error;
        }
        return ParseResult::Ok;
    }

    // 行首空白属于 obs-fold（RFC 7230 已废弃），直接拒绝
    if (line[0] == ' ' || line[0] == '\t') {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    const auto colon = std::ranges::find(line, ':');
    if (colon == line.end() || colon == line.begin()) {
        statusCode_ = 400;
        return ParseResult::Error;
    }

    // header 名统一转小写（大小写不敏感），并拒绝名中非法字符
    std::string name(line.begin(), colon);
    for (char& c : name) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        } else if (!(std::isalnum(static_cast<unsigned char>(c)) || c == '-' || c == '_' || c == '.')) {
            statusCode_ = 400;
            return ParseResult::Error;
        }
    }

    // 跳过值前空白
    auto valueStart = colon + 1;
    while (valueStart != line.end() && isspace(static_cast<unsigned char>(*valueStart))) {
        ++valueStart;
    }
    std::string value(valueStart, line.end());
    // 去除值尾空白
    while (!value.empty() && isspace(static_cast<unsigned char>(value.back()))) {
        value.pop_back();
    }

    // 重复 Header 合并（逗号连接），Connection/Content-Length 由各自逻辑处理
    if (auto it = headers_.find(name); it != headers_.end()) {
        it->second += ", " + value;
    } else {
        headers_.emplace(std::move(name), std::move(value));
    }
    return ParseResult::Ok;
}

void HttpRequest::parsePost() {
    const auto it = headers_.find("content-type");
    if (it == headers_.end() || method_ != Method::Post) {
        return;
    }
    // Content-Type 可能带参数（如 charset=utf-8），只取媒体类型部分
    const std::string mediaType = it->second.substr(0, it->second.find(';'));
    if (mediaType != "application/x-www-form-urlencoded") {
        LOGW << "Unsupported content type: " << mediaType;
        return;
    }
    // 对请求体做 URL 解码（'+' 为空格）；非法编码 -> 400
    std::string decoded;
    if (!urlDecode(content_, true, decoded)) {
        statusCode_ = 400;
        return;
    }
    content_ = std::move(decoded);
    parseFromUrlEncoded();

    if (path_ == "/login.html") {
        LOGI << "User login: " << path_;
        if (userVerify(posts_["username"], posts_["password"], true)) {
            LOGI << "User login success";
            path_ = "/index.html";
        } else {
            LOGW << "User login failed";
            path_ = "/error.html";
        }
    } else if (path_ == "/register.html") {
        LOGI << "User register: " << path_;
        if (userVerify(posts_["username"], posts_["password"], false)) {
            LOGI << "User register success";
            path_ = "/index.html";
        } else {
            LOGW << "User register failed";
            path_ = "/error.html";
        }
    } else {
        LOGW << "Unknown POST request to: " << path_;
        path_ = "/error.html";
    }
}

void HttpRequest::parseFromUrlEncoded() {
    if (content_.empty()) {
        LOGW << "Content is empty";
        return;
    }

    // content_ 已由 urlDecode 完成 percent 解码与 '+' -> 空格，这里只需按 & 与 = 切分
    size_t begin = 0;
    while (begin <= content_.size()) {
        const size_t amp = content_.find('&', begin);
        const size_t end = (amp == std::string::npos) ? content_.size() : amp;
        const std::string pair = content_.substr(begin, end - begin);
        if (!pair.empty()) {
            const size_t eq = pair.find('=');
            if (eq == std::string::npos) {
                posts_[pair] = "";
            } else {
                posts_[pair.substr(0, eq)] = pair.substr(eq + 1);
            }
        }
        if (amp == std::string::npos) {
            break;
        }
        begin = amp + 1;
    }
}

bool HttpRequest::userVerify(const std::string& name, const std::string& pwd, const bool isLogin) {
    if (name.empty() || pwd.empty()) {
        LOGI << "name.empty() || pwd.empty()";
        return false;
    }
    LOGI << "Verify user: " << name;

    // conn 是一个 RAII Connection 对象
    const auto conn = SqlConnPool::getConnection();

    bool flag = false;
    if (!isLogin) {
        flag = true;
    }

    // 检查并创建 user 表（幂等）
    LOGD << "Check and create user table";
    const auto createTableQuery = R"(
        CREATE TABLE IF NOT EXISTS `user` (
            username VARCHAR(255) NOT NULL PRIMARY KEY,
            password VARCHAR(255) NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )";

    if (mysql_query(conn.get(), createTableQuery)) {
        LOGE << "Failed to create table: user - " << mysql_error(conn.get());
        return false;
    }

    // 使用预处理语句避免 SQL 注入
    LOGD << "mysql_stmt_init";
    MYSQL_STMT* stmt = mysql_stmt_init(conn.get());
    if (!stmt) {
        LOGE << "mysql_stmt_init failed";
        return false;
    }

    if (const auto query = "SELECT username, password FROM `user` WHERE username=? LIMIT 1";
        mysql_stmt_prepare(stmt, query, strlen(query))) {
        LOGE << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    MYSQL_BIND bind[2]{};
    my_bool name_is_null = 0;
    my_bool pwd_is_null = 0;

    // 绑定输入参数
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char*>(name.c_str());
    bind[0].buffer_length = name.length();
    bind[0].is_null = &name_is_null;

    if (mysql_stmt_bind_param(stmt, bind)) {
        LOGE << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_execute(stmt)) {
        LOGE << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定结果集
    std::memset(bind, 0, sizeof(bind));
    char db_username[256]{};
    char db_password[256]{};
    unsigned long username_length = 0;
    unsigned long password_length = 0;
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = db_username;
    bind[0].buffer_length = sizeof(db_username);
    bind[0].length = &username_length;
    bind[0].is_null = &name_is_null;

    bind[1].buffer_type = MYSQL_TYPE_STRING;
    bind[1].buffer = db_password;
    bind[1].buffer_length = sizeof(db_password);
    bind[1].length = &password_length;
    bind[1].is_null = &pwd_is_null;

    if (mysql_stmt_bind_result(stmt, bind)) {
        LOGE << "mysql_stmt_bind_result failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    if (mysql_stmt_store_result(stmt)) {
        LOGE << "mysql_stmt_store_result failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        std::string password(db_password, password_length);
        // 登陆行为 且 密码正确
        if (isLogin) {
            if (pwd == password) {
                flag = true;
            } else {
                flag = false;
                LOGE << "Password mismatch for user: " << name;
            }
        } else {
            // 注册行为：用户名已被占用
            flag = false;
            LOGI << "User already exists: " << name;
        }
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    // 注册行为 且 用户名未被使用 -> 插入新用户
    if (!isLogin && flag) {
        LOGI << "Register new user: " << name;
        stmt = mysql_stmt_init(conn.get());
        if (!stmt) {
            LOGE << "mysql_stmt_init failed";
            return false;
        }

        if (const auto insert_query = "INSERT INTO `user`(username, password) VALUES(?, ?)";
            mysql_stmt_prepare(stmt, insert_query, strlen(insert_query))) {
            LOGE << "mysql_stmt_prepare failed: " << mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return false;
        }

        MYSQL_BIND insert_bind[2]{};

        // 绑定用户名
        insert_bind[0].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[0].buffer = const_cast<char*>(name.c_str());
        insert_bind[0].buffer_length = name.length();
        insert_bind[0].is_null = &name_is_null;

        // 绑定密码
        insert_bind[1].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[1].buffer = const_cast<char*>(pwd.c_str());
        insert_bind[1].buffer_length = pwd.length();
        insert_bind[1].is_null = &pwd_is_null;

        if (mysql_stmt_bind_param(stmt, insert_bind)) {
            LOGE << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return false;
        }

        if (mysql_stmt_execute(stmt)) {
            LOGE << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
            mysql_stmt_close(stmt);
            return false;
        }

        flag = true;
        mysql_stmt_close(stmt);
    }
    LOGD << "UserVerify result: " << (flag ? "ok" : "failed");
    return flag;
}

int HttpRequest::hexCovert(const char ch) {
    if (ch >= '0' && ch <= '9') {
        return ch - '0';
    }
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return -1;
}

void HttpRequest::clear() {
    state_ = ParseState::ParseLine;
    path_ = query_ = content_ = "";
    headers_.clear();
    posts_.clear();
    statusCode_ = 200;
    contentLength_ = 0;
}

bool HttpRequest::validateContentLength() {
    const auto it = headers_.find("content-length");
    if (it == headers_.end()) {
        // 无 Content-Length：无请求体
        contentLength_ = 0;
        return true;
    }

    // 多个 Content-Length 已被合并为逗号串；只有完全一致时才接受
    const std::string& value = it->second;
    if (value.find(',') != std::string::npos) {
        statusCode_ = 400;
        return false;
    }
    if (value.empty() || !std::all_of(value.begin(), value.end(), [](unsigned char c) { return std::isdigit(c); })) {
        statusCode_ = 400;
        return false;
    }

    size_t length = 0;
    for (const char c : value) {
        const size_t digit = static_cast<size_t>(c - '0');
        if (length > (maxBodySize - digit) / 10) {
            // 溢出或超过上限 -> 413
            statusCode_ = 413;
            return false;
        }
        length = length * 10 + digit;
    }

    if (length > maxBodySize) {
        statusCode_ = 413;
        return false;
    }
    contentLength_ = length;
    return true;
}

bool HttpRequest::isChunked() const {
    const auto it = headers_.find("transfer-encoding");
    if (it == headers_.end()) {
        return false;
    }
    // 值统一小写后检测 chunked（含 "gzip, chunked" 等组合）
    std::string value = it->second;
    std::transform(
        value.begin(), value.end(), value.begin(), [](unsigned char c) { return static_cast<char>(std::tolower(c)); });
    return value.find("chunked") != std::string::npos;
}

bool HttpRequest::urlDecode(const std::string& input, const bool plusAsSpace, std::string& output) {
    output.clear();
    output.reserve(input.size());
    for (size_t i = 0; i < input.size(); ++i) {
        const char c = input[i];
        if (c == '%') {
            // 越界防护：% 必须在串尾前至少两个字节
            if (i + 2 >= input.size() + 0 && i + 2 > input.size() - 1) {
                return false;
            }
            if (i + 2 >= input.size()) {
                // 剩余不足两个十六进制字符
                if (i + 1 < input.size() && hexCovert(input[i + 1]) < 0) {
                    return false;
                }
                return false;
            }
            const int hi = hexCovert(input[i + 1]);
            const int lo = hexCovert(input[i + 2]);
            if (hi < 0 || lo < 0) {
                return false;
            }
            const char decoded = static_cast<char>((hi << 4) | lo);
            if (decoded == '\0') {
                // %00 会在 C 字符串/文件路径中截断，直接拒绝
                return false;
            }
            output.push_back(decoded);
            i += 2;
        } else if (plusAsSpace && c == '+') {
            output.push_back(' ');
        } else {
            output.push_back(c);
        }
    }
    return true;
}

bool HttpRequest::sanitizePath(const std::string& rawPath, std::string& outPath) {
    outPath.clear();
    if (rawPath.empty() || rawPath[0] != '/') {
        return false;
    }
    // 含 NUL 或控制字符直接拒绝（防路径注入）
    for (const unsigned char c : rawPath) {
        if (c == '\0' || (c < 0x20 && c != '\t')) {
            return false;
        }
    }

    // 按 '/' 分段；忽略重复斜杠与 "/./"，拒绝 "/../" 与结尾 "/.."
    std::string normalized;
    size_t i = 0;
    while (i < rawPath.size()) {
        // 跳过重复斜杠
        while (i < rawPath.size() && rawPath[i] == '/') {
            ++i;
        }
        const size_t segBegin = i;
        while (i < rawPath.size() && rawPath[i] != '/') {
            ++i;
        }
        const std::string seg = rawPath.substr(segBegin, i - segBegin);
        if (seg.empty() || seg == ".") {
            continue;
        }
        if (seg == "..") {
            // 越界（.. 已把路径推回根之上）-> 拒绝
            return false;
        }
        normalized += "/";
        normalized += seg;
    }
    if (normalized.empty()) {
        normalized = "/";
    }

    // 目录请求（原始路径以 '/' 结尾）-> 默认文档
    if (rawPath.back() == '/') {
        normalized += (normalized.back() == '/') ? "index.html" : "/index.html";
    }

    // 兼容旧行为：无扩展名的已知 HTML 补 .html
    const size_t lastSlash = normalized.find_last_of('/');
    const size_t lastDot = normalized.find_last_of('.');
    const bool hasExt = (lastDot != std::string::npos) && (lastDot > lastSlash);
    if (!hasExt && ALL_HTML.contains(normalized)) {
        normalized += ".html";
    }

    outPath = normalized;
    return true;
}

ParseResult HttpRequest::parse(Buffer& buff) {
    // 无数据可读：等待更多数据
    if (buff.readableSize() == 0) {
        return ParseResult::NeedMore;
    }

    while (state_ != ParseState::ParseFinish) {
        if (state_ == ParseState::ParseContent) {
            // 请求体阶段：按 Content-Length 字节流读取，绝不按行切分
            if (contentLength_ == 0) {
                state_ = ParseState::ParseFinish;
                break;
            }
            const size_t available = buff.readableSize();
            if (available == 0) {
                return ParseResult::NeedMore;
            }
            const size_t take = std::min(available, contentLength_ - content_.size());
            content_.append(buff.peek(), take);
            buff.retrieve(take);
            if (content_.size() >= contentLength_) {
                state_ = ParseState::ParseFinish;
                break;
            }
            // 请求体未完整到达：等待更多数据
            return ParseResult::NeedMore;
        }

        // 请求行 / Header 阶段：按行读取
        const char* lineEnd = findLineEnd(buff.peek(), buff.beginWrite());
        if (lineEnd == nullptr) {
            // 无完整行：若已积累超限则拒绝，否则等待更多数据
            if (buff.readableSize() > maxHeaderLine) {
                statusCode_ = 413;
                return ParseResult::Error;
            }
            return ParseResult::NeedMore;
        }
        const size_t lineLen = static_cast<size_t>(lineEnd - buff.peek());
        if (lineLen > maxHeaderLine) {
            statusCode_ = 413;
            return ParseResult::Error;
        }
        const std::string line(buff.peek(), lineEnd);

        ParseResult result = ParseResult::Ok;
        if (state_ == ParseState::ParseLine) {
            if (lineLen > maxUriLength) {
                statusCode_ = 414;
                return ParseResult::Error;
            }
            result = parseRequestLine(line);
            if (result == ParseResult::Ok) {
                state_ = ParseState::ParseHeaders;
            }
        } else {
            // ParseHeaders
            if (headers_.size() >= maxHeaderCount) {
                statusCode_ = 413;
                return ParseResult::Error;
            }
            result = parseHeaders(line);
        }

        buff.retrieveUntil(lineEnd + 2);
        if (result != ParseResult::Ok) {
            return result;
        }
    }

    // 解析完成：POST 表单处理
    if (state_ == ParseState::ParseFinish) {
        if (method_ == Method::Post) {
            parsePost();
            if (statusCode_ != 200) {
                // body 解码失败等错误：返回 Error 状态码
                return ParseResult::Error;
            }
        } else {
            // 非 POST 方法的请求体按协议读取并忽略（防请求走私）
        }
        LOGD << "[" << getMethod() << "] [" << path_ << "] [" << getVersion() << "]";
    }
    return ParseResult::Ok;
}

void HttpRequest::preloadAllHtml(const std::string& rootPath, const bool recursive) {
    ALL_HTML.clear();
    try {
        if (!std::filesystem::exists(rootPath) || !std::filesystem::is_directory(rootPath)) {
            LOGE << "Invalid directory: " << rootPath;
            return;
        }
        const auto collect = [&rootPath](const auto& it) {
            for (const auto& entry : it) {
                if (entry.is_regular_file() && entry.path().extension() == ".html") {
                    std::filesystem::path relativePath = std::filesystem::relative(entry.path(), rootPath);
                    std::string relativeStr = relativePath.string();
                    // 去除 .html 后缀
                    relativeStr = relativeStr.substr(0, relativeStr.size() - 5);
                    if (relativeStr.empty() || relativeStr[0] != '/') {
                        relativeStr.insert(relativeStr.begin(), '/');
                    }
                    ALL_HTML.insert(relativeStr);
                }
            }
        };
        if (recursive) {
            collect(std::filesystem::recursive_directory_iterator(rootPath));
        } else {
            collect(std::filesystem::directory_iterator(rootPath));
        }
    } catch (const std::filesystem::filesystem_error& e) {
        LOGE << "Filesystem error: " << e.what();
    } catch (const std::exception& e) {
        LOGE << "General exception: " << e.what();
    }
}

bool HttpRequest::isKeepAlive() const {
    bool hasClose = false;
    bool hasKeepAlive = false;
    if (const auto it = headers_.find("connection"); it != headers_.end()) {
        // 值可能为逗号分隔的多个 token（如 "keep-alive, Upgrade"）
        std::string value = it->second;
        std::transform(value.begin(), value.end(), value.begin(), [](unsigned char c) {
            return static_cast<char>(std::tolower(c));
        });
        // 精确匹配逗号分隔的 token 列表
        size_t begin = 0;
        while (begin <= value.size()) {
            const size_t comma = value.find(',', begin);
            const size_t end = (comma == std::string::npos) ? value.size() : comma;
            const std::string token = value.substr(begin, end - begin);
            if (token == "close") {
                hasClose = true;
            } else if (token == "keep-alive") {
                hasKeepAlive = true;
            }
            if (comma == std::string::npos) {
                break;
            }
            begin = comma + 1;
        }
    }
    if (version_ == Version::Http11) {
        // HTTP/1.1 默认 keep-alive，除非显式 close
        return !hasClose;
    }
    // HTTP/1.0 默认关闭，除非显式 keep-alive
    return hasKeepAlive;
}
