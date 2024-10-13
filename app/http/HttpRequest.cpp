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

#include "HttpRequest.hpp"

#include <cstring>
#include <filesystem>
#include <mysql.h>

#include "../logger/Logger.hpp"
#include "../pool/SqlConnPool.hpp"

bool HttpRequest::parseRequestLine(const std::string &line) {
    // 当向 http://example.com/path?arg1=1&arg2=2 发起 GET 请求时，请求报文结构如下
    // GET /path?arg1=1&arg2=2 HTTP/1.1
    // Host: example.com
    // User-Agent: [客户端软件标识]
    // Accept: [可接受的内容类型]
    // [其他可选的请求头]

    auto start = line.begin();
    const auto end = line.end();

    auto blank = std::find(start, end, ' ');
    if (blank == end) {
        return false;
    }
    if (!parseMethod(std::string(start, blank))) {
        return false;
    }

    start = blank + 1;
    blank = std::find(start, end, ' ');
    if (blank == end) {
        return false;
    }

    if (const auto query = std::find(start, end, '?'); query == end) {
        path_.assign(start, blank);
    } else {
        path_.assign(start, query);
        query_.assign(query + 1, blank);
    }

    if (path_ == "/") {
        path_ = "/index.html";
    } else {
        if (ALL_HTML.contains(path_)) {
            path_ += ".html";
        }
    }

    start = blank + 1;
    if (const auto version = std::string(start, end); version == VERSION_STR.at(Version::Http10)) {
        version_ = Version::Http10;
    } else if (version == VERSION_STR.at(Version::Http11)) {
        version_ = Version::Http11;
    } else {
        version_ = Version::Unknown;
        return false;
    }

    return true;
}

bool HttpRequest::parseMethod(const std::string &line) {
    if (line == "GET") {
        method_ = Method::Get;
    } else if (line == "POST") {
        method_ = Method::Post;
    } else if (line == "HEAD") {
        method_ = Method::Head;
    } else if (line == "PUT") {
        method_ = Method::Put;
    } else if (line == "DELETE") {
        method_ = Method::Delete;
    } else if (line == "CONNECT") {
        method_ = Method::Connect;
    } else if (line == "OPTIONS") {
        method_ = Method::Options;
    } else if (line == "TRACE") {
        method_ = Method::Trace;
    } else {
        return false;
    }

    return true;
}

bool HttpRequest::parseHeaders(const std::string &line) {
    if (line.empty()) {
        return false;
    }
    const auto colon = std::ranges::find(line, ':');
    if (colon == line.end()) {
        return false;
    }
    auto valueStart = colon + 1;
    while (valueStart != line.end() && isspace(*valueStart)) {
        ++valueStart;
    }
    headers_[std::string(line.begin(), colon)] = std::string(valueStart, line.end());

    return true;
}

void HttpRequest::parseContent(const std::string &line) {
    content_ = line;
    parsePost();
    LOGD << "Content:" << line << ", len:" << line.size();
}

void HttpRequest::parsePost() {
    std::string contentType;
    auto it = headers_.find("Content-Type");
    if (it == headers_.end()) {
        it = headers_.find("content-type");
        if (it == headers_.end()) {
            return;
        }
    }
    if (method_ == Method::Post && it->second == "application/x-www-form-urlencoded") {
        // 解析 POST 请求参数
        parseFromUrlEncoded();
        // 如果是登录/注册的path
        if (path_ == "/login.html") {
            LOGI << "User login: " << path_;
            if (userVerify(posts_["username"], posts_["password"], true)) {
                // 登陆成功
                LOGI << "User login success";
                path_ = "/index.html";
            } else {
                // 出错
                LOGW << "User login failed";
                path_ = "/error.html";
            }
        } else if (path_ == "/register.html") {
            LOGI << "User register" << path_;
            if (userVerify(posts_["username"], posts_["password"], false)) {
                // 注册成功
                LOGI << "User register success";
                path_ = "/index.html";
            } else {
                // 出错
                LOGW << "User register failed";
                path_ = "/error.html";
            }
        } else {
            // 出错
            LOGW << "Unknown POST requests";
            path_ = "/error.html";
        }
    }
}

void HttpRequest::parseFromUrlEncoded() {
    if (content_.empty()) {
        LOGW << __FUNCTION__ << ": Content is empty";
        return;
    }

    std::string key{}, value{};
    int num;
    const size_t n = content_.size();
    size_t i = 0, j = 0;

    for (; i < n; i++) {
        switch (content_[i]) {
            case '=':
                key = content_.substr(j, i - j);
                j = i + 1;
                break;
            case '+':
                content_[i] = ' ';
                break;
            case '%':
                num = hexCovert(content_[i + 1]) * 16 + hexCovert(content_[i + 2]);
                content_[i + 2] = static_cast<char>(num % 10 + '0');
                content_[i + 1] = static_cast<char>(num / 10 + '0');
                i += 2;
                break;
            case '&':
                value = content_.substr(j, i - j);
                j = i + 1;
                posts_[key] = value;
                LOGD << key << " = " << value;
                break;
            default:
                break;
        }
    }
    assert(j <= i);
    if (!posts_.contains(key) && j < i) {
        value = content_.substr(j, i - j);
        posts_[key] = value;
    }
}

bool HttpRequest::userVerify(const std::string &name, const std::string &pwd, const bool isLogin) {
    if (name.empty() || pwd.empty()) {
        LOGI << "name.empty() || pwd.empty()";
        return false;
    }
    LOGI << "Verify name:" << name.c_str() << " pwd:" << pwd.c_str();

    // conn 是一个 RAII Connection 对象
    const auto conn = SqlConnPool::getConnection();

    bool flag = false;
    if (!isLogin) {
        flag = true;
    }

    // 检查并创建 user 表
    const auto createTableQuery = R"(
        CREATE TABLE IF NOT EXISTS `user` (
            username VARCHAR(255) NOT NULL PRIMARY KEY,
            password VARCHAR(255) NOT NULL
        ) ENGINE=InnoDB DEFAULT CHARSET=utf8mb4;
    )";

    if (mysql_query(conn.get(), createTableQuery)) {
        LOGE << "Failed to create table: user";
        return false;
    }

    // 使用预处理语句(更高效)或转义用户输入以避免 SQL 注入攻击
    // 准备预处理语句
    MYSQL_STMT *stmt = mysql_stmt_init(conn.get());
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
    memset(bind, 0, sizeof(bind));
    bind[0].buffer_type = MYSQL_TYPE_STRING;
    bind[0].buffer = const_cast<char *>(name.c_str());
    bind[0].buffer_length = name.length();
    bind[0].is_null = &name_is_null;

    if (mysql_stmt_bind_param(stmt, bind)) {
        LOGE << "mysql_stmt_bind_param failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    // 执行预处理语句
    if (mysql_stmt_execute(stmt)) {
        LOGE << "mysql_stmt_execute failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    // 绑定结果集
    memset(bind, 0, sizeof(bind));
    char db_username[256]{};
    char db_password[256]{};
    unsigned long username_length, password_length;
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

    // 获取结果
    if (mysql_stmt_store_result(stmt)) {
        LOGE << "mysql_stmt_store_result failed: " << mysql_stmt_error(stmt);
        mysql_stmt_close(stmt);
        return false;
    }

    while (mysql_stmt_fetch(stmt) == 0) {
        LOGI << "MYSQL ROW: " << (strlen(db_username) > 0 ? db_username : "NULL") << " "
            << (strlen(db_password) ? db_password : "NULL");
        std::string password(strlen(db_password) ? db_password : "");
        // 登陆行为 且 密码正确
        if (isLogin) {
            if (pwd == password) {
                flag = true;
            } else {
                flag = false;
                LOGE << "MySQL pwd error!";
            }
        } else {
            flag = false;
            LOGI << "user used!";
        }
    }

    mysql_stmt_free_result(stmt);
    mysql_stmt_close(stmt);

    // 注册行为 且 用户名未被使用
    if (!isLogin && flag) {
        LOGI << "User register!";
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
        insert_bind[0].buffer = const_cast<char *>(name.c_str());
        insert_bind[0].buffer_length = name.length();
        insert_bind[0].is_null = &name_is_null;

        // 绑定密码
        insert_bind[1].buffer_type = MYSQL_TYPE_STRING;
        insert_bind[1].buffer = const_cast<char *>(pwd.c_str());
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
    LOGI << "UserVerify success!";
    return flag;
}

int HttpRequest::hexCovert(const char ch) {
    if (ch >= 'A' && ch <= 'F') {
        return ch - 'A' + 10;
    }
    if (ch >= 'a' && ch <= 'f') {
        return ch - 'a' + 10;
    }
    return ch;
}

void HttpRequest::clear() {
    // 状态复位
    state_ = ParseState::ParseLine;
    path_ = query_ = content_ = "";
    headers_.clear();
    posts_.clear();
}

bool HttpRequest::parse(Buffer &buff) {
    static constexpr char END[] = "\r\n";
    // 没有可读的字节
    if (buff.readableSize() == 0) {
        LOGW << "buff.readableSize() == 0";
        return false;
    }
    // 读取数据开始
    while (buff.readableSize() && state_ != ParseState::ParseFinish) {
        // 从 buff 中取出待读取的数据并去除 "\r\n"，返回有效数据的行末指针
        const char *lineEnd = std::search(buff.peek(), const_cast<const Buffer &>(buff).beginWrite(), END, END + 2);
        std::string line(buff.peek(), lineEnd);
        switch (state_) {
            case ParseState::ParseLine:
                LOGD << "parseRequestLine(line): " << line;
                if (!parseRequestLine(line)) {
                    LOGW << "parseRequestLine(line) failed";
                    return false;
                }
                state_ = ParseState::ParseHeaders;
                break;
            case ParseState::ParseHeaders:
                LOGD << "parseHeaders(line): " << line;
                if (!parseHeaders(line)) {
                    state_ = ParseState::ParseContent;
                }
                break;
            case ParseState::ParseContent:
                LOGD << "parseContent(line): " << line;
                parseContent(line);
                state_ = ParseState::ParseFinish;
                break;
            default:
                break;
        }
        // 读完了
        if (lineEnd == buff.beginWrite()) {
            buff.retrieveAll();
            break;
        }
        // 跳过 "\r\n"
        buff.retrieveUntil(lineEnd + 2);
    }
    LOGD << "[" << METHOD_STR.at(method_) << "], [" << path_.c_str() << "], [" << VERSION_STR.at(version_) << "]";
    return true;
}

std::string HttpRequest::path() const {
    return path_;
}

std::string &HttpRequest::path() {
    return path_;
}

std::string HttpRequest::method() const {
    return METHOD_STR.at(method_);
}

std::string HttpRequest::version() const {
    return VERSION_STR.at(version_);
}

std::string HttpRequest::getPost(const std::string &key) const {
    assert(!key.empty());
    if (const auto it = posts_.find(key); it != posts_.end()) {
        return posts_.find(key)->second;
    }
    return "";
}

std::string HttpRequest::getPost(const char *key) const {
    assert(key != nullptr);
    if (const auto it = posts_.find(key); it != posts_.end()) {
        return posts_.find(key)->second;
    }
    return "";
}

bool HttpRequest::isKeepAlive() const {
    if (const auto it = headers_.find("Connection"); it != headers_.end()) {
        return (it->second == "keep-alive" || it->second == "Keep-Alive") && version_ == Version::Http11;
    }
    LOGW << "Connection header not found";
    return false;
}

void HttpRequest::preloadAllHtml(const std::string &rootPath, const bool recursive) {
    // 清空之前的内容，防止重复加载
    ALL_HTML.clear();

    try {
        // 检查 rootPath 是否为有效目录
        if (!std::filesystem::exists(rootPath) || !std::filesystem::is_directory(rootPath)) {
            LOGE << "Invalid directory: " << rootPath;
            return;
        }

        // 根据是否递归选择迭代器类型
        if (recursive) {
            for (const auto &entry: std::filesystem::recursive_directory_iterator(rootPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".html") {
                    std::filesystem::path relativePath = relative(entry.path(), rootPath);
                    std::string relativeStr = relativePath.string();
                    // 去除 .html 后缀，5 是 ".html" 的长度
                    relativeStr = relativeStr.substr(0, relativeStr.size() - 5);
                    // 确保前面有 '/' 符号
                    if (relativeStr.empty() || relativeStr[0] != '/') {
                        std::ranges::reverse(relativeStr);
                        relativeStr.append("/");
                        std::ranges::reverse(relativeStr);
                    }
                    ALL_HTML.insert(relativeStr);
                }
            }
        } else {
            for (const auto &entry: std::filesystem::directory_iterator(rootPath)) {
                if (entry.is_regular_file() && entry.path().extension() == ".html") {
                    std::filesystem::path relativePath = relative(entry.path(), rootPath);
                    std::string relativeStr = relativePath.string();
                    // 去除 .html 后缀，5 是 ".html" 的长度
                    relativeStr = relativeStr.substr(0, relativeStr.size() - 5);
                    // 确保前面有 '/' 符号
                    if (relativeStr.empty() || relativeStr[0] != '/') {
                        std::ranges::reverse(relativeStr);
                        relativeStr.append("/");
                        std::ranges::reverse(relativeStr);
                    }
                    ALL_HTML.insert(relativeStr);
                }
            }
        }
    } catch (const std::filesystem::filesystem_error &e) {
        LOGE << "Filesystem error: " << e.what();
    } catch (const std::exception &e) {
        LOGE << "General exception: " << e.what();
    }
}
