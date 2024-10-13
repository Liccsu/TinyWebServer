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

#include "HttpResponse.hpp"

#include <fcntl.h>
#include <sys/mman.h>

#include "../logger/Logger.hpp"

void HttpResponse::addStateLine(Buffer &buff) {
    if (!CODE_STATUS.contains(code_)) {
        code_ = 400;
    }
    const std::string &status = CODE_STATUS.at(code_);
    buff.append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::addHeader(Buffer &buff) const {
    buff.append("Connection: ");
    if (isKeepAlive_) {
        buff.append("keep-alive\r\n");
        buff.append("keep-alive: max=6, timeout=120\r\n");
    } else {
        buff.append("close\r\n");
    }
    buff.append("Content-type: " + getFileType() + "\r\n");
}

void HttpResponse::addContent(Buffer &buff) {
    const std::string fullPath = srcDir_ + path_;
    const int srcFd = open(fullPath.c_str(), O_RDONLY);
    if (srcFd < 0) {
        errorContent(buff, "File NotFound!");
        return;
    }

    // 获取文件状态
    struct stat fileStat{};
    if (fstat(srcFd, &fileStat) < 0) {
        errorContent(buff, "File Stat Error!");
        close(srcFd);
        return;
    }

    if (fileStat.st_size == 0) {
        errorContent(buff, "Empty File!");
        close(srcFd);
        return;
    }

    LOGD << "file path is '" << fullPath << "'";
    void *mmRet = mmap(nullptr, fileStat.st_size, PROT_READ, MAP_PRIVATE, srcFd, 0);
    if (mmRet == MAP_FAILED) {
        LOGE << "mmap failed";
        errorContent(buff, "File NotFound!");
        close(srcFd);
        return;
    }
    mmFile_ = static_cast<char *>(mmRet);
    // 更新文件状态
    mmFileStat_ = fileStat;

    close(srcFd);
    buff.append("Content-length: " + std::to_string(mmFileStat_.st_size) + "\r\n\r\n");
}

void HttpResponse::errorHtml() {
    if (const auto it = CODE_PATH.find(code_); it != CODE_PATH.end()) {
        path_ = it->second;
        stat((srcDir_ + path_).data(), &mmFileStat_);
    }
}

std::string HttpResponse::getFileType() const {
    const size_t idx = path_.find_last_of('.');
    // 最大值 find 函数在找不到指定值得情况下会返回 std::string::npos
    if (idx == std::string::npos) {
        return "text/plain";
    }
    const std::string suffix = path_.substr(idx);
    if (const auto it = CONTENT_TYPE.find(suffix); it != CONTENT_TYPE.end()) {
        return it->second;
    }
    return "text/plain";
}

void HttpResponse::init(const std::string &srcDir, const std::string &path, const bool isKeepAlive, const int code) {
    assert(!srcDir.empty());
    if (mmFile_) {
        unmapFile();
    }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr;
    mmFileStat_ = {};
}

void HttpResponse::makeResponse(Buffer &buff) {
    if (stat((srcDir_ + path_).data(), &mmFileStat_) < 0 || S_ISDIR(mmFileStat_.st_mode)) {
        LOGW << "File is not found. code: 404";
        code_ = 404;
    } else if (!(mmFileStat_.st_mode & S_IROTH)) {
        LOGW << "File is read by others. code: 403";
        code_ = 403;
    } else if (code_ == -1) {
        code_ = 200;
    }
    errorHtml();
    addStateLine(buff);
    addHeader(buff);
    addContent(buff);
}

void HttpResponse::unmapFile() {
    if (mmFile_) {
        munmap(mmFile_, mmFileStat_.st_size);
        mmFile_ = nullptr;
    }
}

void HttpResponse::errorContent(Buffer &buff, const std::string &message) const {
    std::string body;
    std::string status;
    body += "<html><title>Error</title>";
    body += "<body bgcolor=\"ffffff\">";
    if (const auto it = CODE_STATUS.find(code_); it != CODE_STATUS.end()) {
        status = it->second;
    } else {
        status = "Bad Request";
    }
    body += std::to_string(code_) + " : " + status + "\n";
    body += "<p>" + message + "</p>";
    body += "<hr><em>TinyWebServer</em></body></html>";

    buff.append("Content-length: " + std::to_string(body.size()) + "\r\n\r\n");
    buff.append(body);
}
