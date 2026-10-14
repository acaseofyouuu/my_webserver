#include "config.h"
#include "log/logger.h"
#include "webserver.h"
#include "database/database_pool.h"

#include <cstdlib>
#include <iostream>
#include <pthread.h>
#include <signal.h>
#include <exception>

int run_application(int argc, char *argv[])
{
    Config config;

    if (!config.parse(argc, argv))
    {
        return 1;
    }
    if (config.help) return 0;

    sigset_t signal_mask;
    sigemptyset(&signal_mask);
    sigaddset(&signal_mask, SIGINT);
    sigaddset(&signal_mask, SIGTERM);

    int signal_error =
        pthread_sigmask(SIG_BLOCK, &signal_mask, nullptr);

    if (signal_error != 0)
    {
        std::cerr << "Failed to configure shutdown signals: "
                  << signal_error << '\n';
        return 1;
    }

    if (!Logger::instance().initialize("logs/server.log", config.log_mode == 1, config.close_log == 0))
    {
        return 1;
    }

    Logger::instance().info("Logger initialized");

    const char *database_password =
        std::getenv("WEBSERVER_DB_PASSWORD");

    if (database_password == nullptr)
    {
        std::cerr << "WEBSERVER_DB_PASSWORD is not set\n";
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

    WebServer server(config);

    if (!server.start())
    {
        Logger::instance().error("Failed to start server");
        return 1;
    }

    server.run();

    return 0;
}

int main(int argc, char *argv[])
{
    try { return run_application(argc, argv); }
    catch (const std::exception &error)
    {
        std::cerr << "Server failed: " << error.what() << '\n';
        return 1;
    }
}
