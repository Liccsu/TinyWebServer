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

#ifndef TINYWEBSERVER_HTTPRESPONSE_HPP
#define TINYWEBSERVER_HTTPRESPONSE_HPP

#include <string>
#include <sys/stat.h>
#include <unordered_map>

#include "../net/Buffer.hpp"

class HttpResponse {
    int code_ = -1;
    bool isKeepAlive_ = false;
    bool isHead_ = false;

    // 已通过目录遍历校验的相对路径（以 '/' 开头，不含 '..'）
    std::string path_;
    // 文档根目录（绝对路径）
    std::string srcDir_;

    // mmap 的文件内容与元信息；HEAD 请求不 mmap（仅记录大小）
    char* mmFile_ = nullptr;
    struct stat mmFileStat_{};

    // 内置错误页 HTML（不依赖磁盘文件，保证错误响应永远可用）
    std::string errorBody_;

    // Content Type 类型集
    inline static const std::unordered_map<std::string, std::string> CONTENT_TYPE = {
        {".bmp", "application/x-bmp"},
        {".doc", "application/msword"},
        {".exe", "application/x-msdownload"},
        {".htm", "text/html"},
        {".html", "text/html"},
        {".ico", "image/x-icon"},
        {".java", "java/*"},
        {".latex", "application/x-latex"},
        {".xml", "text/xml"},
        {".xhtml", "application/xhtml+xml"},
        {".txt", "text/plain"},
        {".rtf", "application/rtf"},
        {".pdf", "application/pdf"},
        {".ppt", "application/vnd.ms-powerpoint"},
        {".word", "application/nsword"},
        {".png", "image/png"},
        {".gif", "image/gif"},
        {".jfif", "image/jpeg"},
        {".jpg", "image/jpeg"},
        {".jpeg", "image/jpeg"},
        {".svg", "text/xml"},
        {".au", "audio/basic"},
        {".mpeg", "application/octet-stream"},
        {".mpg", "application/octet-stream"},
        {".mp3", "application/octet-stream"},
        {".mp4", "application/octet-stream"},
        {".mpv", "application/octet-stream"},
        {".avi", "application/octet-stream"},
        {".gz", "application/x-gzip"},
        {".tar", "application/x-tar"},
        {".css", "text/css"},
        {".js", "application/x-javascript"},
        {".torrent", "application/x-bittorrent"},
        {".wav", "application/octet-stream"},
        {".xsl", "text/xml"},
        {".xslt", "text/xml"},
        {".apk", "application/vnd.android.package-archive"},
        {".ipa", "application/vnd.iphone"}};
    // 编码状态集
    inline static const std::unordered_map<int, std::string> CODE_STATUS = {{200, "OK"},
                                                                            {400, "Bad Request"},
                                                                            {403, "Forbidden"},
                                                                            {404, "Not Found"},
                                                                            {405, "Method Not Allowed"},
                                                                            {413, "Payload Too Large"},
                                                                            {414, "URI Too Long"},
                                                                            {500, "Internal Server Error"},
                                                                            {501, "Not Implemented"}};

    void addStateLine(Buffer& buff) const;

    void addHeader(Buffer& buff) const;

    // 追加响应头（Content-length 等）与文件内容（mmap 已由 makeResponse 建立）
    void addContent(Buffer& buff);

    // 生成内置错误页 HTML 到 errorBody_
    void errorHtml();

    // 校验并打开文件：目录遍历 / 符号链接逃逸防御 + mmap。
    // 失败时设置 code_（404/403/500）并返回 false。
    bool resolveFile();

    [[nodiscard]]
    std::string getFileType() const;

public:
    HttpResponse() = default;

    ~HttpResponse() { unmapFile(); }

    // path 必须已经过 HttpRequest::sanitizePath 校验（相对站点根、无 '..'）
    void init(const std::string& srcDir, const std::string& path, bool isKeepAlive, int code, bool isHead);

    void makeResponse(Buffer& buff);

    // 生成纯文本 200 响应（健康检查等），不访问文件系统
    void makeTextResponse(Buffer& buff, const std::string& text, bool keepAlive, bool isHead);

    void unmapFile();

    [[nodiscard]]
    char* file() const {
        return mmFile_;
    }

    [[nodiscard]]
    size_t fileLen() const {
        return static_cast<size_t>(mmFileStat_.st_size);
    }

    [[nodiscard]]
    int code() const {
        return code_;
    }
};

#endif // TINYWEBSERVER_HTTPRESPONSE_HPP
