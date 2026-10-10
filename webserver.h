#pragma once

#include "threadpool/thread_pool.h"
#include <chrono>
#include <mutex>
#include <unordered_map>

class WebServer
{
public:
    explicit WebServer(int port);
    ~WebServer();

    bool start();
    void run();

private:
    int port_;
    int server_fd_;
    int epoll_fd_;
    ThreadPool thread_pool_;
    std::unordered_map<int, std::chrono::steady_clock::time_point>
        client_last_active_;

    std::mutex client_activity_mutex_;

    void mark_client_idle(int client_fd);
    void remove_client_from_idle(int client_fd);
    void close_idle_connections();
};