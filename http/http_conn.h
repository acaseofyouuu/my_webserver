#pragma once

class HttpConnection
{
public:
    explicit HttpConnection(int client_fd);

    void handle();

private:
    int client_fd_;
};