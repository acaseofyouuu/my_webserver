#include "database_pool.h"
#include "../log/logger.h"

DatabasePool &DatabasePool::instance()
{
    static DatabasePool pool;
    return pool;
}

bool DatabasePool::initialize(const std::string &host,
                              const std::string &user,
                              const std::string &password,
                              const std::string &database,
                              unsigned int port,
                              std::size_t pool_size)
{
    if (pool_size == 0)
    {
        Logger::instance().error(
            "Database pool size must be positive");
        return false;
    }

    std::lock_guard<std::mutex> lock(mutex_);

    for (std::size_t i = 0; i < pool_size; ++i)
    {
        MYSQL *connection = mysql_init(nullptr);

        if (connection == nullptr)
        {
            Logger::instance().error(
                "Failed to create MySQL connection object");

            while (!connections_.empty())
            {
                mysql_close(connections_.front());
                connections_.pop();
            }

            return false;
        }

        mysql_options(connection,
                      MYSQL_SET_CHARSET_NAME,
                      "utf8mb4");

        if (mysql_real_connect(connection,
                               host.c_str(),
                               user.c_str(),
                               password.c_str(),
                               database.c_str(),
                               port,
                               nullptr,
                               0) == nullptr)
        {
            Logger::instance().error(
                std::string("Failed to connect to MySQL: ") +
                mysql_error(connection));

            mysql_close(connection);

            while (!connections_.empty())
            {
                mysql_close(connections_.front());
                connections_.pop();
            }

            return false;
        }

        connections_.push(connection);
    }

    Logger::instance().info(
        "Database pool initialized with " +
        std::to_string(pool_size) +
        " connections");

    return true;
}

MYSQL *DatabasePool::acquire()
{
    std::unique_lock<std::mutex> lock(mutex_);

    condition_.wait(lock, [this]
                    { return !connections_.empty(); });

    MYSQL *connection = connections_.front();
    connections_.pop();

    return connection;
}

void DatabasePool::release(MYSQL *connection)
{
    if (connection == nullptr)
    {
        return;
    }

    {
        std::lock_guard<std::mutex> lock(mutex_);
        connections_.push(connection);
    }

    condition_.notify_one();
}

DatabasePool::~DatabasePool()
{
    std::lock_guard<std::mutex> lock(mutex_);

    while (!connections_.empty())
    {
        mysql_close(connections_.front());
        connections_.pop();
    }
}

DatabaseConnectionGuard::DatabaseConnectionGuard()
    : connection_(DatabasePool::instance().acquire())
{
}

DatabaseConnectionGuard::~DatabaseConnectionGuard()
{
    DatabasePool::instance().release(connection_);
}

MYSQL *DatabaseConnectionGuard::get() const
{
    return connection_;
}