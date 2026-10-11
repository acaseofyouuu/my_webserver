#include "config.h"
#include "log/logger.h"
#include "webserver.h"

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