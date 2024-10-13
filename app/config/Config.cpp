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

#include "Config.hpp"

#include <fstream>
#include <iostream>

// 创建一个默认配置文件
void Config::create_default_config(const std::filesystem::path &path) {
    // 如果目录不存在则创建
    if (const auto dir_path = path.parent_path(); !exists(dir_path)) {
        create_directories(dir_path);
    }
    std::ofstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not create config file: " + path.string());
    }

    file << R"(
server:
    # 监听端口
    port: 6666
    # 连接超时时间，单位毫秒
    timeout: 60000

mysql:
    # MySQL HOST
    host: 127.0.0.1
    # MySQL PORT
    port: 3306
    # MySQL 用户名
    user: root
    # MySQL 密码
    password: 123456
    # MySQL 数据库名
    db: tiny_web_server_db
    # 数据库连接池初始大小
    pool_size: 12
    # 数据库连接池最低大小
    pool_min_size: 6
    # 数据库连接池最高大小
    pool_max_size: 24

log:
    # 日志文件输出目录
    directory: ./log
    # 日志输出级别 1:debug 2:info 3:warning 4:error 5:none
    level: 2
    # 单个日志文件大小限制，单位 MB
    size: 64
    # 日志文件名 (base name)
    basename: tiny_web_server
    # 是否输出彩色，当选择输出到文件时建议关闭
    colorful: false
    # 是否输出到文件 true:输出到文件 false:输出到终端
    output_to_file: true

site:
    # 静态网站目录
    path: ./dist
)";
    file.close();
}

Config::Config(const std::string &path) {
    // 如果配置文件不存在则创建默认配置文件
    if (const std::filesystem::path &file_path(std::filesystem::absolute(path)); !exists(file_path)) {
        std::cout << file_path << " is not exists, will be created" << std::endl;
        create_default_config(file_path);
    }
    std::ifstream file(path);
    if (!file.is_open()) {
        throw std::runtime_error("Could not open config file: " + path);
    }
    // 加载配置文件
    config_ = YAML::Load(file);
    if (!config_) {
        throw std::runtime_error("Failed to parse YAML from file: " + path);
    }
}
