#include "config.h"
#include "log/logger.h"
#include "webserver.h"
#include "database/database_pool.h"

#include <cstdlib>

int main(int argc, char *argv[])
{
    Config config;

    if (!config.parse(argc, argv))
    {
        return 1;
    }

    if (!Logger::instance().initialize("logs/server.log"))
    {
        return 1;
    }

    Logger::instance().info("Logger initialized");

    const char *database_password =
        std::getenv("WEBSERVER_DB_PASSWORD");

    if (database_password == nullptr)
    {
        Logger::instance().error(
            "WEBSERVER_DB_PASSWORD is not set");
        return 1;
    }

    if (!DatabasePool::instance().initialize(
            config.database_host,
            config.database_user,
            database_password,
            config.database_name,
            config.database_port,
            config.database_pool_size))
    {
        return 1;
    }

    {
        DatabaseConnectionGuard connection;

        if (mysql_ping(connection.get()) != 0)
        {
            Logger::instance().error(
                std::string("Database connection test failed: ") +
                mysql_error(connection.get()));
            return 1;
        }

        Logger::instance().info(
            "Database connection borrowed successfully");
    }

    WebServer server(config.port,
                     config.thread_count,
                     config.idle_timeout_seconds);

    if (!server.start())
    {
        Logger::instance().error("Failed to start server");
        return 1;
    }

    server.run();

    return 0;
}