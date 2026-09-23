#include "http_conn.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>

HttpConnection::HttpConnection(int client_fd)
    : client_fd_(client_fd)
{
}

void HttpConnection::handle()
{
    std::cout << "Client connected , fd = " << client_fd_ << '\n';

    char buffer[4096]{};

    ssize_t bytes_read = read(client_fd_, buffer, sizeof(buffer) - 1);

    if (bytes_read == -1)
    {
        std::cerr << "Failed to read client request\n";
        close(client_fd_);
        return;
    }

    if (bytes_read == 0)
    {
        std::cout << "Client closed the connection before sending data\n";
        close(client_fd_);
        return;
    }
    buffer[bytes_read] = '\0';

    std::cout << "Received request:\n"
              << buffer << '\n';

    std::istringstream request_stream(buffer);

    std::string method;
    std::string request_path;
    std::string http_version;

    request_stream >> method >> request_path >> http_version;

    std::cout << "Method: " << method << '\n';
    std::cout << "Path: " << request_path << '\n';
    std::cout << "HTTP version: " << http_version << '\n';

    std::string file_path;
    std::string status_line;

    if (request_path == "/")
    {
        file_path = "root/index.html";
        status_line = "HTTP/1.1 200 OK\r\n";
    }
    else if (request_path == "/about.html")
    {
        file_path = "root/about.html";
        status_line = "HTTP/1.1 200 OK\r\n";
    }
    else
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";
    }

    std::ifstream html_file(file_path);

    if (!html_file.is_open())
    {
        std::cerr << "Failed to open " << file_path << '\n';
        close(client_fd_);
        return;
    }

    std::string body;
    std::string line;

    while (std::getline(html_file, line))
    {
        body += line;
        body += '\n';
    }

    std::string response = status_line;
    response += "Content-Type: text/html; charset=UTF-8\r\n";
    response += "Content-Length: " + std::to_string(body.size()) + "\r\n";
    response += "Connection: close\r\n";
    response += "\r\n";
    response += body;

    ssize_t bytes_sent =
        write(client_fd_, response.c_str(), response.size());

    if (bytes_sent == -1)
    {
        std::cerr << "Failed to send response\n";
        close(client_fd_);
        return;
    }

    std::cout << "HTTP response sent\n";

    close(client_fd_);
}