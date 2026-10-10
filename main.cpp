#include "config.h"
#include "webserver.h"

int main(int argc, char *argv[])
{
    Config config;

    if (!config.parse(argc, argv))
    {
        return 1;
    }

    WebServer server(config.port,
                     config.thread_count,
                     config.idle_timeout_seconds);

    if (!server.start())
    {
        return 1;
    }

    server.run();

    return 0;
}