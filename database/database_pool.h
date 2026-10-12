#pragma once

#include <mysql/mysql.h>

#include <condition_variable>
#include <cstddef>
#include <mutex>
#include <queue>
#include <string>

class DatabasePool
{
public:
    static DatabasePool &instance();

    bool initialize(const std::string &host,
                    const std::string &user,
                    const std::string &password,
                    const std::string &database,
                    unsigned int port,
                    std::size_t pool_size);

    MYSQL *acquire();
    void release(MYSQL *connection);

private:
    DatabasePool() = default;
    ~DatabasePool();

    DatabasePool(const DatabasePool &) = delete;
    DatabasePool &operator=(const DatabasePool &) = delete;

    std::queue<MYSQL *> connections_;
    std::mutex mutex_;
    std::condition_variable condition_;
};

class DatabaseConnectionGuard
{
public:
    DatabaseConnectionGuard();
    ~DatabaseConnectionGuard();

    MYSQL *get() const;

private:
    DatabaseConnectionGuard(
        const DatabaseConnectionGuard &) = delete;

    DatabaseConnectionGuard &operator=(
        const DatabaseConnectionGuard &) = delete;

    MYSQL *connection_;
};