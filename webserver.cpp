#include "webserver.h"
#include "http/http_conn.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>

WebServer::WebServer(int port)
    : port_(port),
      server_fd_(-1)
{
}

WebServer::~WebServer()
{
    if (server_fd_ != -1)
    {
        close(server_fd_);
    }
}

bool WebServer::start()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd_ == -1)
    {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    std::cout << "Socket created successfully , fd = " << server_fd_ << '\n';

    int reuse_address = 1;

    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) == -1)
    {
        std::cerr << "Failed to set socket options\n";
        return false;
    }

    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<sockaddr *>(&server_address), sizeof(server_address)) == -1)
    {
        std::cerr << "Failed to bind socket\n";
        return false;
    }

    std::cout << "Socket bound to port " << port_ << '\n';

    if (listen(server_fd_, 10) == -1)
    {
        std::cerr << "Failed to listen on socket\n";
        return false;
    }

    std::cout << "Server is listening on port " << port_ <<'\n';

    return true;
}

void WebServer::run()
{
    while (true)
    {
        std::cout << "Waiting for a client..." << std::endl;

        int client_fd = accept(server_fd_, nullptr, nullptr);

        if (client_fd == -1)
        {
            std::cerr << "Failed to accept client\n";
            continue;
        }

        HttpConnection connection(client_fd);
        connection.handle();
    }
}