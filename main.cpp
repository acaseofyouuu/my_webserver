#include "webserver.h"

int main()
{
    WebServer server(8080);

    if (!server.start())
    {
        return 1;
    }

    server.run();

    return 0;
}