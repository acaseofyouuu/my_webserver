#pragma once

#include <string>

class HttpConnection
{
public:
    explicit HttpConnection(int client_fd);

    void handle();

private:
    int client_fd_;
    bool send_all(const std::string &data);
    bool read_request(std::string &request);
};