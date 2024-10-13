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

#ifndef TINYWEBSERVER_CONFIG_HPP
#define TINYWEBSERVER_CONFIG_HPP

#include <filesystem>
#include <string>

#include "yaml-cpp/yaml.h"

class Config {
    YAML::Node config_;

    explicit Config(const std::string &path);

    ~Config() = default;

    // 读取指定配置项的值
    template<typename T>
    [[nodiscard]]
    T _get(const std::string &key) const {
        std::istringstream iss(key);
        std::string part;
        YAML::Node node = Clone(config_);
        // 根据 '.' 分隔节点
        while (std::getline(iss, part, '.')) {
            if (!node.IsMap() || !node[part]) {
                throw std::runtime_error("Key not found in config: " + key);
            }
            node = node[part];
        }

        return node.as<T>();
    }

    static void create_default_config(const std::filesystem::path &path);

    [[nodiscard]]
    static Config &instance() {
        static Config config("./config/config.yml");
        return config;
    }

public:
    Config() = delete;

    // 禁止拷贝
    Config(const Config &) = delete;

    Config &operator=(const Config &) = delete;

    // 读取指定配置项的值，如果配置文件不存在，将会创建默认配置文件
    template<typename T>
    [[nodiscard]]
    static T get(const std::string &key) {
        return instance()._get<T>(key);
    }
};


#endif //TINYWEBSERVER_CONFIG_HPP
