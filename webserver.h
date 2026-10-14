#pragma once
#include "config.h"
#include "http/http_conn.h"
#include "threadpool/thread_pool.h"
#include <chrono>
#include <cstdint>
#include <map>
#include <memory>
#include <mutex>
#include <queue>
#include <unordered_map>

class WebServer
{
public:
    explicit WebServer(const Config &config);
    WebServer(int port, std::size_t threads, int idle_seconds);
    ~WebServer();
    bool start();
    void run();
private:
    using Next = HttpConnection::Next;
    using Clock = std::chrono::steady_clock;
    using Timers = std::multimap<Clock::time_point, std::uint64_t>;
    struct Client {
        std::shared_ptr<HttpConnection> http;
        bool busy = false, expired = false;
        Next interest = Next::Read;
        Timers::iterator timer;
    };
    struct Completion { std::uint64_t id; Next next; };
    Config config_;
    int server_fd_ = -1, epoll_fd_ = -1, signal_fd_ = -1, completion_fd_ = -1;
    ThreadPool thread_pool_;
    std::uint64_t next_id_ = 3; // 0 listener, 1 signalfd, 2 eventfd
    std::unordered_map<std::uint64_t, Client> clients_;
    Timers timers_;
    std::mutex completion_mutex_;
    std::queue<Completion> completions_;
    bool stopping_ = false;
    Clock::time_point stop_deadline_;
    bool add_fd(int fd, std::uint64_t id, unsigned events);
    void accept_clients();
    void dispatch(std::uint64_t id, Next action);
    void apply_result(std::uint64_t id, Next next);
    void drain_completions();
    void close_client(std::uint64_t id);
    void touch(std::uint64_t id);
    void expire_clients();
};
