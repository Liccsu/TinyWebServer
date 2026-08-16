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

#include "HttpResponse.hpp"

#include <cassert>
#include <climits>
#include <cstring>
#include <fcntl.h>
#include <sys/mman.h>
#include <unistd.h>

#include "../log/Logger.hpp"

void HttpResponse::addStateLine(Buffer& buff) const {
    const std::string& status = CODE_STATUS.contains(code_) ? CODE_STATUS.at(code_) : CODE_STATUS.at(500);
    buff.append("HTTP/1.1 " + std::to_string(code_) + " " + status + "\r\n");
}

void HttpResponse::addHeader(Buffer& buff) const {
    buff.append("Connection: ");
    if (isKeepAlive_) {
        buff.append("keep-alive\r\n");
    } else {
        buff.append("close\r\n");
    }
    buff.append("Content-type: " + getFileType() + "\r\n");
}

void HttpResponse::addContent(Buffer& buff) {
    if (code_ >= 400 || !errorBody_.empty()) {
        // 内置错误页 / 文本响应（healthz）：直接输出，不访问文件系统
        buff.append("Content-length: " + std::to_string(errorBody_.size()) + "\r\n\r\n");
        if (!isHead_) {
            buff.append(errorBody_);
        }
        return;
    }

    if (mmFile_ == nullptr) {
        // HEAD 请求：只有头部与 Content-length，无 body
        buff.append("Content-length: " + std::to_string(fileLen()) + "\r\n\r\n");
        return;
    }
    buff.append("Content-length: " + std::to_string(fileLen()) + "\r\n\r\n");
}

void HttpResponse::errorHtml() {
    const std::string& status = CODE_STATUS.contains(code_) ? CODE_STATUS.at(code_) : "Internal Server Error";
    errorBody_ = "<!DOCTYPE html><html><head><meta charset=\"utf-8\"><title>";
    errorBody_ += std::to_string(code_) + " " + status;
    errorBody_ += "</title></head><body bgcolor=\"#ffffff\"><center><h1>";
    errorBody_ += std::to_string(code_) + " " + status;
    errorBody_ += "</h1></center><hr><center>TinyWebServer</center></body></html>";
}

std::string HttpResponse::getFileType() const {
    const size_t idx = path_.find_last_of('.');
    if (idx == std::string::npos) {
        return "text/plain";
    }
    const std::string suffix = path_.substr(idx);
    if (const auto it = CONTENT_TYPE.find(suffix); it != CONTENT_TYPE.end()) {
        return it->second;
    }
    return "text/plain";
}

void HttpResponse::init(
    const std::string& srcDir, const std::string& path, const bool isKeepAlive, const int code, const bool isHead) {
    assert(!srcDir.empty());
    assert(!path.empty() && path[0] == '/');
    if (mmFile_) {
        unmapFile();
    }
    code_ = code;
    isKeepAlive_ = isKeepAlive;
    isHead_ = isHead;
    path_ = path;
    srcDir_ = srcDir;
    mmFile_ = nullptr;
    mmFileStat_ = {};
    errorBody_.clear();
}

bool HttpResponse::resolveFile() {
    // 目录遍历/符号链接逃逸防御：
    // 0) 字符串级兜底：拒绝任何含 ".." 路径段（HttpRequest::sanitizePath 之外的纵深防御），
    //    越界请求无论目标是否存在一律 403（不泄露文件存在性）
    // 1) 解析真实路径（跟随符号链接）
    // 2) 校验真实路径必须位于文档根目录之内
    // 3) 校验为普通文件且可读
    size_t segBegin = 1; // 跳过开头 '/'
    while (segBegin <= path_.size()) {
        const size_t slash = path_.find('/', segBegin);
        const size_t segEnd = (slash == std::string::npos) ? path_.size() : slash;
        const std::string seg = path_.substr(segBegin, segEnd - segBegin);
        if (seg == "..") {
            LOGW << "Path contains '..' segment, rejected: " << path_;
            code_ = 403;
            return false;
        }
        if (slash == std::string::npos) {
            break;
        }
        segBegin = slash + 1;
    }

    const std::string fullPath = srcDir_ + path_;

    char resolved[PATH_MAX];
    if (realpath(fullPath.c_str(), resolved) == nullptr) {
        // 文件不存在
        code_ = 404;
        return false;
    }
    const std::string realPath(resolved);

    // 文档根（绝对路径，去掉尾部斜杠统一比较）
    std::string root = srcDir_;
    while (root.size() > 1 && root.back() == '/') {
        root.pop_back();
    }

    const bool insideRoot =
        realPath == root ||
        (realPath.size() > root.size() && realPath.compare(0, root.size(), root) == 0 && realPath[root.size()] == '/');
    if (!insideRoot) {
        // 符号链接指向文档根之外（或路径逃逸）
        LOGW << "Path escapes document root: " << fullPath << " -> " << realPath;
        code_ = 403;
        return false;
    }

    if (stat(resolved, &mmFileStat_) < 0) {
        code_ = 404;
        return false;
    }
    if (S_ISDIR(mmFileStat_.st_mode)) {
        code_ = 404;
        return false;
    }
    if (!(mmFileStat_.st_mode & S_IROTH)) {
        code_ = 403;
        return false;
    }

    // HEAD 请求不需要文件内容（body 不发送），仅保留大小供 Content-length 使用
    if (isHead_) {
        return true;
    }
    if (mmFileStat_.st_size == 0) {
        // 空文件：body 为空但响应仍为 200（原行为返回 "Empty File!" 错误，不必要）
        return true;
    }

    const int srcFd = open(resolved, O_RDONLY | O_CLOEXEC);
    if (srcFd < 0) {
        LOGE << "open failed: " << resolved << " - " << strerror(errno);
        code_ = 500;
        return false;
    }

    void* mmRet = mmap(nullptr, static_cast<size_t>(mmFileStat_.st_size), PROT_READ, MAP_PRIVATE, srcFd, 0);
    close(srcFd);
    if (mmRet == MAP_FAILED) {
        LOGE << "mmap failed: " << resolved << " - " << strerror(errno);
        code_ = 500;
        return false;
    }
    mmFile_ = static_cast<char*>(mmRet);
    LOGD << "Serving file: " << fullPath;
    return true;
}

void HttpResponse::makeResponse(Buffer& buff) {
    if (code_ < 400) {
        // 正常请求路径：校验并映射文件
        if (!resolveFile()) {
            // 失败时 code_ 已置为 404/403/500，继续走错误页
        }
    }

    if (code_ >= 400) {
        errorHtml();
    } else {
        code_ = 200;
    }
    addStateLine(buff);
    addHeader(buff);
    addContent(buff);
}

void HttpResponse::makeTextResponse(Buffer& buff, const std::string& text, const bool keepAlive, const bool isHead) {
    if (mmFile_) {
        unmapFile();
    }
    code_ = 200;
    isKeepAlive_ = keepAlive;
    isHead_ = isHead;
    path_ = "/healthz"; // getFileType -> text/plain
    errorBody_ = text;  // 复用 addContent 的内置体输出路径
    addStateLine(buff);
    addHeader(buff);
    addContent(buff);
}

void HttpResponse::unmapFile() {
    if (mmFile_) {
        munmap(mmFile_, static_cast<size_t>(mmFileStat_.st_size));
        mmFile_ = nullptr;
    }
}
