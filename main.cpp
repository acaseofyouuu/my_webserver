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
    std::cout << "Waiting for a client..." << std::endl;

    int client_fd = accept(server_fd, nullptr, nullptr);

    if (client_fd == -1)
    {
        std::cerr << "Failed to accept client\n";
        close(server_fd);
        return 1;
    }

    std::cout << "Client connected , fd = " << client_fd << '\n';

    close(client_fd);
    close(server_fd);
    return 0;
}