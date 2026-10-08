#pragma once

#include <string>
#include <unordered_map>

class HttpConnection
{
public:
    explicit HttpConnection(int client_fd);

    bool handle();

private:
    int client_fd_;
    bool send_all(const std::string &data);
    bool read_request(std::string &request);
    bool parse_request_line(const std::string &request,
                            std::string &method,
                            std::string &request_path,
                            std::string &http_version);

    bool parse_headers(
        const std::string &request,
        std::unordered_map<std::string, std::string> &headers);

    bool should_keep_alive(
        const std::string &http_version,
        const std::unordered_map<std::string, std::string> &headers);
    
};