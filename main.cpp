#include "http/http_conn.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

int main()
{
    int server_fd = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd == -1)
    {
        std::cerr << "Failed to create socket\n";
        return 1;
    }

    std::cout << "Socket created successfully , fd = " << server_fd << '\n';

    int reuse_address = 1;

    if (setsockopt(server_fd,
                   SOL_SOCKET,
                   SO_REUSEADDR,
                   &reuse_address,
                   sizeof(reuse_address)) == -1)
    {
        std::cerr << "Failed to set socket options\n";
        close(server_fd);
        return 1;
    }

    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(8080);

    if (bind(server_fd, reinterpret_cast<sockaddr *>(&server_address), sizeof(server_address)) == -1)
    {
        std::cerr << "Failed to bind socket\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Socket bound to port 8080\n";

    if (listen(server_fd, 10) == -1)
    {
        std::cerr << "Failed to listen on socket\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Server is listening on port 8080\n";

    while (true)
    {
        std::cout << "Waiting for a client..." << std::endl;

        int client_fd = accept(server_fd, nullptr, nullptr);

        if (client_fd == -1)
        {
            std::cerr << "Failed to accept client\n";
            close(server_fd);
            return 1;
        }

        HttpConnection connection(client_fd);
        connection.handle();
    }

    close(server_fd);
    return 0;
}