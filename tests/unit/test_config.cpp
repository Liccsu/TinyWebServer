#include <gtest/gtest.h>

#include <filesystem>
#include <fstream>
#include <stdexcept>

#include "config/Config.hpp"

// Config 为单例，首次 get 时构造并读取 ./config/config.yml（相对进程 cwd）。
// 测试套件启动前（第一个用例运行前）准备配置文件。
class ConfigTest : public ::testing::Test {
protected:
    static void SetUpTestSuite() {
        std::filesystem::create_directories("./config");
        std::ofstream f("./config/config.yml");
        f << R"(
server:
    port: 6666
    timeout: 60000

mysql:
    host: 127.0.0.1
    port: 3306
    user: root
    password: secret
    db: test_db
    pool_size: 2
    pool_min_size: 1
    pool_max_size: 4

log:
    level: 2
    size: 64
    basename: test_log
    colorful: false
    output_to_file: false

site:
    path: ./dist
)";
    }
};

TEST_F(ConfigTest, ReadsValidValues) {
    EXPECT_EQ(Config::get<int>("server.port"), 6666);
    EXPECT_EQ(Config::get<int>("server.timeout"), 60000);
    EXPECT_EQ(Config::get<std::string>("site.path"), "./dist");
    EXPECT_EQ(Config::get<std::string>("mysql.user"), "root");
    EXPECT_EQ(Config::get<bool>("log.output_to_file"), false);
    EXPECT_EQ(Config::get<int>("mysql.pool_size"), 2);
}

TEST_F(ConfigTest, MissingKeyThrows) {
    EXPECT_THROW(static_cast<void>(Config::get<int>("server.nonexistent")), std::runtime_error);
    EXPECT_THROW(static_cast<void>(Config::get<std::string>("log.nonexistent")), std::runtime_error);
}

TEST_F(ConfigTest, TypeMismatchThrows) {
    // 值存在但类型不符 -> 抛异常（启动时给出明确错误）
    EXPECT_THROW(static_cast<void>(Config::get<int>("site.path")), std::exception);
    EXPECT_THROW(static_cast<void>(Config::get<bool>("server.port")), std::exception);
}

TEST_F(ConfigTest, NestedKeyResolution) {
    EXPECT_EQ(Config::get<int>("mysql.pool_min_size"), 1);
    EXPECT_EQ(Config::get<int>("mysql.pool_max_size"), 4);
}

TEST_F(ConfigTest, OptionalKeyDefault) {
    // 缺失键 -> 默认值
    EXPECT_EQ(Config::getWithDefault<int>("server.threads", 0), 0);
    EXPECT_EQ(Config::getWithDefault<int>("server.max_connections", 65536), 65536);
    EXPECT_EQ(Config::getWithDefault<std::string>("server.host", "0.0.0.0"), "0.0.0.0");
    EXPECT_EQ(Config::getWithDefault<size_t>("server.max_body_size", 1024), 1024);
}

TEST_F(ConfigTest, OptionalKeyTypeErrorStillThrows) {
    // 键存在但类型不符：即使有默认值也必须报错（配置错误不能静默忽略）
    EXPECT_THROW(static_cast<void>(Config::getWithDefault<int>("site.path", 42)), std::exception);
}
