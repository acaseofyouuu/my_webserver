#pragma once
class WebServer{
public:
    explicit WebServer(int port);
    ~WebServer();

    bool start();
    void run();
private:
    int port_;
    int server_fd_;
};