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
#include <sstream>
#include <stdexcept>
#include <string>

#include "yaml-cpp/yaml.h"

// 配置键不存在（getWithDefault 据此返回默认值）
class ConfigKeyMissing : public std::runtime_error {
public:
    explicit ConfigKeyMissing(const std::string& key)
        : std::runtime_error("config: 配置项缺失: " + key) {}
};

class Config {
    YAML::Node config_;

    explicit Config(const std::string& path);

    ~Config() = default;

    // 读取指定配置项的值；键不存在抛 ConfigKeyMissing，类型不符抛 std::runtime_error
    template <typename T>
    [[nodiscard]]
    T _get(const std::string& key) const {
        std::istringstream iss(key);
        std::string part;
        YAML::Node node = Clone(config_);
        // 根据 '.' 分隔节点
        while (std::getline(iss, part, '.')) {
            if (!node.IsMap() || !node[part]) {
                throw ConfigKeyMissing(key);
            }
            node = node[part];
        }
        try {
            return node.as<T>();
        } catch (const YAML::Exception& e) {
            throw std::runtime_error("config: 配置项类型错误: " + key + " (" + e.what() + ")");
        }
    }

    static void create_default_config(const std::filesystem::path& path);

    [[nodiscard]]
    static Config& instance() {
        static Config config("./config/config.yml");
        return config;
    }

public:
    Config() = delete;

    // 禁止拷贝
    Config(const Config&) = delete;

    Config& operator=(const Config&) = delete;

    // 读取指定配置项的值，如果配置文件不存在，将会创建默认配置文件
    template <typename T>
    [[nodiscard]]
    static T get(const std::string& key) {
        return instance()._get<T>(key);
    }

    // 读取可选配置项：键缺失时返回默认值；键存在但类型错误时仍抛异常
    template <typename T>
    [[nodiscard]]
    static T getWithDefault(const std::string& key, const T& defaultValue) {
        try {
            return instance()._get<T>(key);
        } catch (const ConfigKeyMissing&) {
            return defaultValue;
        }
    }
};

#endif // TINYWEBSERVER_CONFIG_HPP