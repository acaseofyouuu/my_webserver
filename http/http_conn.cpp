#include "http_conn.h"

#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <unistd.h>
#include <filesystem>

namespace
{
    std::string get_content_type(const std::string &file_path)
    {
        std::string extension =
            std::filesystem::path(file_path).extension().string();

        if (extension == ".html")
        {
            return "text/html; charset=UTF-8";
        }

        if (extension == ".css")
        {
            return "text/css; charset=UTF-8";
        }

        if (extension == ".js")
        {
            return "application/javascript; charset=UTF-8";
        }

        if (extension == ".png")
        {
            return "image/png";
        }

        if (extension == ".jpg" || extension == ".jpeg")
        {
            return "image/jpeg";
        }

        if (extension == ".gif")
        {
            return "image/gif";
        }

        if (extension == ".svg")
        {
            return "image/svg+xml";
        }

        if (extension == ".ico")
        {
            return "image/x-icon";
        }

        return "application/octet-stream";
    }
}

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

    if (request_path == "/")
    {
        request_path = "/index.html";
    }

    bool invalid_path =
        request_path.empty() || request_path.front() != '/' ||
        request_path.find("..") != std::string::npos;

    std::string file_path;
    std::string status_line;

    if (invalid_path)
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";
    }
    else
    {
        file_path = "root" + request_path;
        status_line = "HTTP/1.1 200 OK\r\n";
    }

    std::ifstream file(file_path, std::ios::binary);

    if (!file.is_open() && !invalid_path)
    {
        file_path = "root/404.html";
        status_line = "HTTP/1.1 404 Not Found\r\n";

        file.clear();
        file.open(file_path, std::ios::binary);
    }

    if (!file.is_open())
    {
        std::cerr << "Failed to open " << file_path << '\n';
        close(client_fd_);
        return;
    }

    std::ostringstream body_stream;
    body_stream << file.rdbuf();

    std::string body = body_stream.str();
    std::string content_type = get_content_type(file_path);

    std::string response = status_line;
    response += "Content-Type: " + content_type + "\r\n";
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