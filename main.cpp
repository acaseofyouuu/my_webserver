#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <string>
#include <fstream>

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

        std::cout << "Client connected , fd = " << client_fd << '\n';

        char buffer[4096]{};

        ssize_t bytes_read = read(client_fd, buffer, sizeof(buffer) - 1);

        if (bytes_read == -1)
        {
            std::cerr << "Failed to read client request\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }

        std::cout << "Received request:\n"
                  << buffer << '\n';

        std::ifstream html_file("root/index.html");

        if (!html_file.is_open())
        {
            std::cerr << "Failed to open root/index.html\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }

        std::string body;
        std::string line;

        while (std::getline(html_file, line))
        {
            body += line;
            body += '\n';
        }

        std::string response = "HTTP/1.1 200 OK\r\n";
        response += "Content-Type: text/html; charset=UTF-8\r\n";
        response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
        response += "Connection: close\r\n";
        response += "\r\n";
        response += body;

        ssize_t bytes_sent =
            write(client_fd, response.c_str(), response.size());

        if (bytes_sent == -1)
        {
            std::cerr << "Failed to send response\n";
            close(client_fd);
            close(server_fd);
            return 1;
        }

        std::cout << "HTTP response sent\n";

        close(client_fd);
    }
    close(server_fd);
    return 0;
}