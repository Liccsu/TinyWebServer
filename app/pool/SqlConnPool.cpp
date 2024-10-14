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

#include "SqlConnPool.hpp"

#include <mysql/mysqld_error.h>

#include "../logger/Logger.hpp"

SqlConnPool::SqlConnPool() {
    // 读取配置
    host_ = Config::get<std::string>("mysql.host");
    port_ = Config::get<uint16_t>("mysql.port");
    user_ = Config::get<std::string>("mysql.user");
    pwd_ = Config::get<std::string>("mysql.password");
    dbName_ = Config::get<std::string>("mysql.db");
    minPoolSize_ = Config::get<int>("mysql.pool_min_size");
    maxPoolSize_ = Config::get<int>("mysql.pool_max_size");

    // 第一步：确保数据库存在
    if (!ensureDatabase()) {
        LOGE << "Failed to ensure database exists. Initialization aborted";
        throw std::runtime_error("Database initialization failed.");
    }

    // 第二步：初始化连接池
    const auto connSize = Config::get<int>("mysql.pool_size");
    for (int i = 0; i < connSize; ++i) {
        if (MysqlPtr conn = createConnection()) {
            {
                std::unique_lock writeLock(poolMutex_);
                connQue_.emplace(conn.release());
                ++currentPoolSize_;
                ++availableConnections_;
            }
            cond_.notify_one();
        } else {
            LOGE << "Failed to create MySQL connection during pool initialization";
        }
    }

    // 启动监控线程
    shutdown_ = false;
    monitorThread_ = std::thread(&SqlConnPool::monitorPool, this);

    LOGI << "SqlConnPool initialized with " << currentPoolSize_ << " connections";
}

MysqlPtr SqlConnPool::createConnection() const {
    MYSQL *conn = mysql_init(nullptr);
    if (!conn) {
        LOGE << "MySQL init failed!";
        return nullptr;
    }

    if (!mysql_real_connect(conn, host_.c_str(), user_.c_str(), pwd_.c_str(),
                            dbName_.c_str(), port_, nullptr, 0)) {
        LOGE << "MySQL connect failed: " << mysql_error(conn);
        mysql_close(conn);
        return nullptr;
    }

    // 设置连接选项自动重连
    constexpr my_bool reconnect = 1;
    mysql_options(conn, MYSQL_OPT_RECONNECT, &reconnect);

    return MysqlPtr(conn);
}

SqlConnPool::Connection SqlConnPool::_getConnection() {
    std::unique_lock lock(poolMutex_);

    if (connQue_.empty() && currentPoolSize_ >= maxPoolSize_) {
        // 等待有连接可用或允许扩展连接池
        cond_.wait(lock, [this]() {
            return !connQue_.empty() || currentPoolSize_ < maxPoolSize_;
        });
    }
    // 如果有空闲的连接
    if (!connQue_.empty()) {
        // 从队列中取出一个连接
        MYSQL *conn = connQue_.front();
        connQue_.pop();
        --availableConnections_;
        usedConn_.emplace_back(conn);
        return {conn, *this};
    }
    // 如果没有空闲的连接且当前连接池大小小于设定的最大连接池大小
    if (currentPoolSize_ < maxPoolSize_) {
        // 则动态扩展连接池
        if (MysqlPtr newConn = createConnection()) {
            MYSQL *rawConn = newConn.release();
            ++currentPoolSize_;
            usedConn_.emplace_back(rawConn);
            return {rawConn, *this};
        }
        throw std::runtime_error("Failed to create new MySQL connection.");
    }
    throw std::runtime_error("No available MySQL connections.");
}

void SqlConnPool::releaseConnection(MYSQL *conn) {
    std::unique_lock lock(poolMutex_);
    // 找到对应的连接并释放
    if (const auto it = std::ranges::find(usedConn_, conn); it != usedConn_.end()) {
        usedConn_.erase(it);
        connQue_.emplace(conn);
        ++availableConnections_;
        lock.unlock();
        cond_.notify_one();
    } else {
        LOGE << "Attempted to release a connection that is not in use";
    }
}

bool SqlConnPool::ensureDatabase() const {
    MYSQL *tempConn = mysql_init(nullptr);
    if (!tempConn) {
        LOGE << "MySQL init failed!";
        return false;
    }

    // 连接不指定数据库
    if (!mysql_real_connect(tempConn, host_.c_str(), user_.c_str(), pwd_.c_str(),
                            nullptr, port_, nullptr, 0)) {
        LOGE << "MySQL connect failed: " << mysql_error(tempConn);
        mysql_close(tempConn);
        return false;
    }

    // 尝试选择指定数据库，检查是否存在
    if (mysql_select_db(tempConn, dbName_.c_str()) == 0) {
        // 数据库已存在
        LOGD << "Database `" << dbName_ << "` exists";
        mysql_close(tempConn);
        return true;
    }
    if (const auto error_code = mysql_errno(tempConn); error_code == ER_BAD_DB_ERROR) {
        // 数据库不存在
        LOGE << "Database `" << dbName_ << "` does not exist. Attempting to create it...";
        const std::string createDBQuery = "CREATE DATABASE `" + dbName_ +
                                          "` CHARACTER SET utf8mb4 COLLATE utf8mb4_general_ci;";
        if (mysql_query(tempConn, createDBQuery.c_str())) {
            LOGE << "Failed to create database: " << mysql_error(tempConn);
            mysql_close(tempConn);
            return false;
        }
        LOGD << "Database `" << dbName_ << "` created successfully";
        mysql_close(tempConn);
        return true;
    }
    // 其他连接错误
    LOGE << "Failed to select database: " << mysql_error(tempConn);
    mysql_close(tempConn);
    return false;
}

void SqlConnPool::monitorPool() {
    while (!shutdown_) {
        std::unique_lock lock(poolMutex_);
        // 健康检查：检查可用连接
        std::queue<MYSQL *> tempQueue;
        while (!connQue_.empty()) {
            MYSQL *conn = connQue_.front();
            connQue_.pop();

            if (mysql_ping(conn) != 0) {
                LOGE << "MySQL connection lost: " << mysql_error(conn) << ". Attempting to reconnect...";
                // 尝试重连
                if (MysqlPtr newConn = createConnection()) {
                    LOGD << "Reconnected to MySQL successfully";
                    tempQueue.emplace(newConn.release());
                } else {
                    LOGE << "Failed to reconnect to MySQL. Removing connection from pool";
                    --currentPoolSize_;
                    --availableConnections_;
                    // 不将该连接放回池中
                }
            } else {
                tempQueue.emplace(conn);
            }
        }
        // 交换回原来的队列
        connQue_ = std::move(tempQueue);

        // 动态收缩连接池
        if (availableConnections_ > minPoolSize_) {
            const int connectionsToRemove = availableConnections_ - minPoolSize_;
            for (int i = 0; i < connectionsToRemove; ++i) {
                if (!connQue_.empty()) {
                    MYSQL *conn = connQue_.front();
                    connQue_.pop();
                    // 使用 unique_ptr 自动管理资源
                    MysqlPtr ptr(conn);
                    --currentPoolSize_;
                    --availableConnections_;
                }
            }
            LOGD << "Shrunk connection pool to " << currentPoolSize_ << " connections";
        }

        // TODO: 如果某一段时间内连接使用率超过阈值，则增加连接数

        // 每60秒检查一次
        std::this_thread::sleep_for(std::chrono::seconds(60));
    }
}

SqlConnPool::~SqlConnPool() {
    shutdown_ = true;
    if (monitorThread_.joinable()) {
        // 唤醒监控线程
        cond_.notify_all();
        monitorThread_.join();
    }

    // 关闭所有连接
    {
        std::unique_lock lock(poolMutex_);
        while (!connQue_.empty()) {
            MYSQL *conn = connQue_.front();
            connQue_.pop();
            if (conn) {
                // 使用 unique_ptr 自动管理资源
                MysqlPtr ptr(conn);
            }
        }
        // 关闭使用中的连接
        for (MYSQL *conn: usedConn_) {
            if (conn) {
                MysqlPtr ptr(conn);
            }
        }
        usedConn_.clear();
    }
}
