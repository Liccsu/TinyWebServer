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
        Connection(MYSQL *conn, SqlConnPool &pool) :
                conn_(conn),
                pool_(pool) {
        }

        // 析构时释放连接
        ~Connection() {
            if (conn_ && !moved_) {
                pool_.releaseConnection(conn_);
            }
        }

        // 禁止拷贝
        Connection(const Connection &) = delete;

        Connection &operator=(const Connection &) = delete;

        // 移动构造
        Connection(Connection &&other) noexcept:
                conn_(other.conn_),
                pool_(other.pool_),
                moved_(other.moved_) {
            other.conn_ = nullptr;
            other.moved_ = true;
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
        LOGD << "SqlConnPool::getConnection()";
        return sqlConnPool._getConnection();
    }
};


#endif //TINYWEBSERVER_SQLCONNPOOL_HPP
