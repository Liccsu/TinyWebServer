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

#ifndef TINYWEBSERVER_SQLCONNPOOL_HPP
#define TINYWEBSERVER_SQLCONNPOOL_HPP

#include <condition_variable>
#include <list>
#include <memory>
#include <queue>
#include <shared_mutex>
#include <string>
#include <thread>
#include <mysql/mysql.h>

#include "../config/Config.hpp"
#include "../logger/Logger.hpp"

// 自定义删除器，用于确保连接被正确关闭
struct MysqlDeleter {
    void operator()(MYSQL *conn) const {
        if (conn) {
            mysql_close(conn);
            LOGD << "MySQL connection closed";
        }
    }
};

using MysqlPtr = std::unique_ptr<MYSQL, MysqlDeleter>;

class SqlConnPool {
    // RAII 连接包装类
    class Connection {
        MYSQL *conn_;
        SqlConnPool &pool_;
        bool moved_ = false;

    public:
        Connection(MYSQL *conn, SqlConnPool &pool): conn_(conn), pool_(pool) {
        }

        // 禁止拷贝
        Connection(const Connection &) = delete;

        Connection &operator=(const Connection &) = delete;

        // 移动构造
        Connection(Connection &&other) noexcept: conn_(other.conn_), pool_(other.pool_), moved_(other.moved_) {
            other.conn_ = nullptr;
            other.moved_ = true;
        }

        // 析构时释放连接
        ~Connection() {
            if (conn_ && !moved_) {
                pool_.releaseConnection(conn_);
            }
        }

        // 移动赋值
        Connection &operator=(Connection &&other) noexcept {
            if (this != &other) {
                if (conn_ && !moved_) {
                    pool_.releaseConnection(conn_);
                }
                conn_ = other.conn_;
                moved_ = other.moved_;
                other.conn_ = nullptr;
                other.moved_ = true;
            }
            return *this;
        }

        // 允许转换为 MYSQL * 类型
        explicit operator MYSQL *() const {
            return conn_;
        }

        [[nodiscard]]
        MYSQL *get() const {
            return conn_;
        }
    };

    // 可用连接队列
    std::queue<MYSQL *> connQue_;
    // 使用中的连接指针列表
    std::list<MYSQL *> usedConn_;

    // 连接池读写锁
    mutable std::shared_mutex poolMutex_;
    // 条件变量，使用 condition_variable_any 以配合 shared_mutex
    std::condition_variable_any cond_;

    // 监控线程
    std::thread monitorThread_;
    bool shutdown_ = false;

    // 数据库连接信息
    std::string host_;
    uint16_t port_;
    std::string user_;
    std::string pwd_;
    std::string dbName_;

    // 连接池大小控制
    int minPoolSize_;
    int maxPoolSize_;

    // 当前连接池大小
    int currentPoolSize_{};
    // 空闲可用的连接数量
    int availableConnections_{};

    SqlConnPool();

    // 创建一个新的 MySQL 连接
    MysqlPtr createConnection() const;

    Connection _getConnection();

    // 释放连接（仅供Connection类调用）
    void releaseConnection(MYSQL *conn);

    // 确保数据库存在，如果不存在则创建
    bool ensureDatabase() const;

    // 监控连接池，动态扩展和收缩，并进行健康检查
    void monitorPool();

public:
    // 禁止拷贝和赋值
    SqlConnPool(const SqlConnPool &) = delete;

    SqlConnPool &operator=(const SqlConnPool &) = delete;

    // 销毁连接池
    ~SqlConnPool();

    // 获取连接（返回 Connection 对象）
    static Connection getConnection() {
        static SqlConnPool sqlConnPool;
        return  sqlConnPool._getConnection();
    }
};

// 数据库连接池测试
// #include <iostream>
// int main() {
//     try {
//         // 获取连接并进行数据库操作
//         {
//             const auto conn = SqlConnPool::getConnection(); // conn 是一个 RAII Connection 对象
//             // 使用 conn 进行数据库操作
//             if (conn.get() && mysql_query(conn.get(), "SELECT VERSION()")) {
//                 std::cerr << "MySQL query error: " << mysql_error(conn.get()) << std::endl;
//             } else {
//                 if (const auto res = mysql_store_result(conn.get())) {
//                     if (const auto row = mysql_fetch_row(res)) {
//                         std::cout << "MySQL Version: " << row[0] << std::endl;
//                     }
//                     mysql_free_result(res);
//                 }
//             }
//             // conn 在这里会被释放（自动归还连接池）
//         }
//
//         // 也可以使用多线程来测试连接池
//         auto worker = [](const int id) {
//             try {
//                 const auto conn = SqlConnPool::getConnection();
//                 const std::string query = "SELECT NOW();";
//                 if (mysql_query(conn.get(), query.c_str())) {
//                     std::cerr << "Thread " << id << " MySQL query error: " << mysql_error(conn.get()) << std::endl;
//                 } else {
//                     if (const auto res = mysql_store_result(conn.get())) {
//                         if (const auto row = mysql_fetch_row(res)) {
//                             std::cout << "Thread " << id << " MySQL NOW(): " << row[0] << std::endl;
//                         }
//                         mysql_free_result(res);
//                     }
//                 }
//                 // conn 在此作用域结束时自动归还
//             } catch (const std::exception &ex) {
//                 std::cerr << "Thread " << id << " Error: " << ex.what() << std::endl;
//             }
//         };
//
//         // 启动多个线程
//         std::vector<std::thread> threads;
//         for (int i = 0; i < 15; ++i) {
//             threads.emplace_back(worker, i);
//         }
//
//         // 等待所有线程完成
//         for (auto &t: threads) {
//             t.join();
//         }
//     } catch (const std::exception &ex) {
//         std::cerr << "Error: " << ex.what() << std::endl;
//     }
//
//     // 程序结束前可以手动销毁连接池，或让析构函数自动处理
//     return 0;
// }


#endif //TINYWEBSERVER_SQLCONNPOOL_HPP
