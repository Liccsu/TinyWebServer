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

#ifndef TINYWEBSERVER_HTTPRESPONSE_HPP
#define TINYWEBSERVER_HTTPRESPONSE_HPP

#include <string>
#include <unordered_map>
#include <sys/stat.h>

#include "../buffer/Buffer.hpp"

class HttpResponse {
    int code_;
    bool isKeepAlive_;

    std::string path_;
    std::string srcDir_;

    char *mmFile_;
    struct stat mmFileStat_;
    // Content Type 类型集
    inline static const std::unordered_map<std::string, std::string> CONTENT_TYPE = {
            {".bmp",     "application/x-bmp"},
            {".doc",     "application/msword"},
            {".exe",     "application/x-msdownload"},
            {".htm",     "text/html"},
            {".html",    "text/html"},
            {".ico",     "image/x-icon"},
            {".java",    "java/*"},
            {".latex",   "application/x-latex"},
            {".xml",     "text/xml"},
            {".xhtml",   "application/xhtml+xml"},
            {".txt",     "text/plain"},
            {".rtf",     "application/rtf"},
            {".pdf",     "application/pdf"},
            {".ppt",     "application/vnd.ms-powerpoint"},
            {".word",    "application/nsword"},
            {".png",     "image/png"},
            {".gif",     "image/gif"},
            {".jfif",    "image/jpeg"},
            {".jpg",     "image/jpeg"},
            {".jpeg",    "image/jpeg"},
            {".svg",     "text/xml"},
            {".au",      "audio/basic"},
            {".mpeg",    "application/octet-stream"},
            {".mpg",     "application/octet-stream"},
            {".mp3",     "application/octet-stream"},
            {".mp4",     "application/octet-stream"},
            {".mpv",     "application/octet-stream"},
            {".avi",     "application/octet-stream"},
            {".gz",      "application/x-gzip"},
            {".tar",     "application/x-tar"},
            {".css",     "text/css"},
            {".js",      "application/x-javascript"},
            {".torrent", "application/x-bittorrent"},
            {".wav",     "application/octet-stream"},
            {".xsl",     "text/xml"},
            {".xslt",    "text/xml"},
            {".apk",     "application/vnd.android.package-archive"},
            {".ipa",     "application/vnd.iphone"}
    };
    // 编码状态集
    inline static const std::unordered_map<int, std::string> CODE_STATUS = {
            {200, "OK"},
            {400, "Bad Request"},
            {403, "Forbidden"},
            {404, "Not Found"}
    };
    // 编码路径集
    inline static const std::unordered_map<int, std::string> CODE_PATH = {
            {400, "/400.html"},
            {403, "/403.html"},
            {404, "/404.html"}
    };

    void addStateLine(Buffer &buff);

    void addHeader(Buffer &buff) const;

    void addContent(Buffer &buff);

    void errorHtml();

    [[nodiscard]]
    std::string getFileType() const;

public:
    HttpResponse() :
            code_(-1),
            isKeepAlive_(false),
            mmFile_(nullptr),
            mmFileStat_() {
    }

    ~HttpResponse() {
        unmapFile();
    }

    void init(const std::string &srcDir, const std::string &path, bool isKeepAlive = false, int code = -1);

    void makeResponse(Buffer &buff);

    void unmapFile();

    [[nodiscard]]
    char *file() const {
        return mmFile_;
    }

    [[nodiscard]]
    size_t fileLen() const {
        return mmFileStat_.st_size;
    }

    void errorContent(Buffer &buff, const std::string &message) const;

    [[nodiscard]]
    int code() const {
        return code_;
    }
};


#endif //TINYWEBSERVER_HTTPRESPONSE_HPP
