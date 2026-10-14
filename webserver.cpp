#include "webserver.h"
#include "log/logger.h"
#include <cerrno>
#include <csignal>
#include <cstring>
#include <iostream>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/eventfd.h>
#include <sys/signalfd.h>
#include <sys/socket.h>
#include <unistd.h>

WebServer::WebServer(const Config &config) : config_(config), thread_pool_(config.thread_count) {}
WebServer::WebServer(int port, std::size_t threads, int idle_seconds)
    : WebServer([&] { Config c; c.port = port; c.thread_count = threads; c.idle_timeout_seconds = idle_seconds; return c; }()) {}
WebServer::~WebServer()
{
    // 先等任务退出，再释放连接；工作线程不会使用已关闭或被复用的 fd。
    thread_pool_.shutdown();
    clients_.clear();
    for (int fd : {server_fd_, signal_fd_, completion_fd_, epoll_fd_}) if (fd >= 0) close(fd);
}
bool WebServer::add_fd(int fd, std::uint64_t id, unsigned events)
{
    epoll_event event{};
    event.events = events;
    event.data.u64 = id;
    return epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, fd, &event) == 0;
}
bool WebServer::start()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    epoll_fd_ = epoll_create1(EPOLL_CLOEXEC);
    completion_fd_ = eventfd(0, EFD_NONBLOCK | EFD_CLOEXEC);
    sigset_t mask;
    sigemptyset(&mask); sigaddset(&mask, SIGINT); sigaddset(&mask, SIGTERM);
    signal_fd_ = signalfd(-1, &mask, SFD_NONBLOCK | SFD_CLOEXEC);
    if (server_fd_ < 0 || epoll_fd_ < 0 || completion_fd_ < 0 || signal_fd_ < 0) return false;
    int reuse = 1;
    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse))) return false;
    sockaddr_in address{};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_ANY);
    address.sin_port = htons(config_.port);
    if (bind(server_fd_, reinterpret_cast<sockaddr *>(&address), sizeof(address)) || listen(server_fd_, 4096))
    { std::cerr << "Listen failed: " << std::strerror(errno) << '\n'; return false; }
    if (!add_fd(server_fd_, 0, EPOLLIN | (config_.trigger_mode >= 2 ? static_cast<unsigned>(EPOLLET) : 0u)) ||
        !add_fd(signal_fd_, 1, EPOLLIN) || !add_fd(completion_fd_, 2, EPOLLIN)) return false;
    Logger::instance().info("Server is listening on port " + std::to_string(config_.port) +
        ", actor=" + std::to_string(config_.actor_model) + ", trigger=" + std::to_string(config_.trigger_mode));
    return true;
}
void WebServer::accept_clients()
{
    do {
        int fd = accept4(server_fd_, nullptr, nullptr, SOCK_NONBLOCK | SOCK_CLOEXEC);
        if (fd < 0)
        {
            if (errno == EINTR) continue;
            if (errno != EAGAIN && errno != EWOULDBLOCK) Logger::instance().error("accept failed");
            return;
        }
        if (clients_.size() >= 65536) { close(fd); continue; }
        linger setting{config_.linger_mode, config_.linger_mode ? 1 : 0};
        if (setsockopt(fd, SOL_SOCKET, SO_LINGER, &setting, sizeof(setting))) { close(fd); continue; }
        const auto id = next_id_++;
        auto http = std::make_shared<HttpConnection>(fd);
        auto timer = timers_.emplace(Clock::now() + std::chrono::seconds(config_.idle_timeout_seconds), id);
        clients_.emplace(id, Client{http, false, false, Next::Read, timer});
        unsigned flags = EPOLLIN | EPOLLRDHUP | EPOLLONESHOT;
        if (config_.trigger_mode % 2) flags |= EPOLLET;
        if (!add_fd(fd, id, flags)) close_client(id);
        if (config_.trigger_mode < 2) return;
    } while (!stopping_);
}
void WebServer::touch(std::uint64_t id)
{
    auto &client = clients_.at(id);
    if (client.timer != timers_.end()) timers_.erase(client.timer);
    client.timer = timers_.emplace(Clock::now() + std::chrono::seconds(config_.idle_timeout_seconds), id);
}
void WebServer::close_client(std::uint64_t id)
{
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    if (it->second.busy) { it->second.expired = true; return; }
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, it->second.http->fd(), nullptr);
    if (it->second.timer != timers_.end()) timers_.erase(it->second.timer);
    clients_.erase(it);
}
void WebServer::expire_clients()
{
    while (!timers_.empty() && timers_.begin()->first <= Clock::now())
    {
        auto id = timers_.begin()->second;
        timers_.erase(timers_.begin());
        clients_.at(id).timer = timers_.end();
        close_client(id);
    }
}
void WebServer::dispatch(std::uint64_t id, Next action)
{
    auto &client = clients_.at(id);
    client.busy = true;
    auto http = client.http;
    bool accepted = thread_pool_.enqueue([this, http, id, action] {
        Next result = Next::Close;
        try {
            if (action == Next::Read) result = http->read_ready(config_.trigger_mode % 2);
            else if (action == Next::Write) result = http->write_ready();
            else result = Next::Process;
            if (result == Next::Process) result = http->process();
        } catch (const std::exception &e) { Logger::instance().error(e.what()); result = Next::Close; }
        catch (...) { result = Next::Close; }
        {
            std::lock_guard<std::mutex> lock(completion_mutex_);
            completions_.push({id, result});
        }
        std::uint64_t wake = 1;
        while (write(completion_fd_, &wake, sizeof(wake)) < 0 && errno == EINTR) {}
    });
    if (!accepted) { client.busy = false; close_client(id); }
}
void WebServer::apply_result(std::uint64_t id, Next next)
{
    auto it = clients_.find(id);
    if (it == clients_.end()) return;
    auto &client = it->second;
    if (client.expired || next == Next::Close || (stopping_ && (next == Next::Read || next == Next::Process)))
    { close_client(id); return; }
    if (next == Next::Process) { dispatch(id, next); return; }
    client.interest = next;
    epoll_event event{};
    event.data.u64 = id;
    event.events = EPOLLONESHOT | EPOLLRDHUP | (next == Next::Write ? EPOLLOUT : EPOLLIN);
    if (config_.trigger_mode % 2) event.events |= EPOLLET;
    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client.http->fd(), &event)) close_client(id);
}
void WebServer::drain_completions()
{
    std::uint64_t count;
    while (read(completion_fd_, &count, sizeof(count)) < 0 && errno == EINTR) {}
    std::queue<Completion> ready;
    { std::lock_guard<std::mutex> lock(completion_mutex_); ready.swap(completions_); }
    while (!ready.empty())
    {
        auto completion = ready.front(); ready.pop();
        auto it = clients_.find(completion.id);
        if (it == clients_.end()) continue;
        it->second.busy = false;
        apply_result(completion.id, completion.next);
    }
}
void WebServer::run()
{
    epoll_event events[256];
    while (!stopping_ || !clients_.empty())
    {
        if (stopping_ && Clock::now() >= stop_deadline_) break;
        int count = epoll_wait(epoll_fd_, events, 256, 100);
        if (count < 0) { if (errno == EINTR) continue; Logger::instance().error("epoll_wait failed"); break; }
        for (int i = 0; i < count; ++i)
        {
            auto id = events[i].data.u64;
            auto flags = events[i].events;
            if (id == 0) { if (!stopping_) accept_clients(); continue; }
            if (id == 1)
            {
                signalfd_siginfo info{};
                if (read(signal_fd_, &info, sizeof(info)) != sizeof(info)) continue;
                Logger::instance().info("Shutdown signal received: " + std::to_string(info.ssi_signo));
                stopping_ = true;
                stop_deadline_ = Clock::now() + std::chrono::seconds(5);
                epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, server_fd_, nullptr);
                for (auto it = clients_.begin(); it != clients_.end();)
                {
                    const auto key = it->first;
                    bool idle = !it->second.busy && it->second.interest == Next::Read;
                    ++it;
                    if (idle) close_client(key);
                }
                continue;
            }
            if (id == 2) { drain_completions(); continue; }
            auto it = clients_.find(id);
            if (it == clients_.end() || it->second.busy) continue;
            if (flags & (EPOLLERR | EPOLLHUP)) { close_client(id); continue; }
            touch(id);
            auto action = it->second.interest;
            if (config_.actor_model == 1) dispatch(id, action);
            else
            {
                // 模拟 Proactor：主线程收发，工作线程解析、查库、准备响应。
                auto result = action == Next::Write ? it->second.http->write_ready() :
                    it->second.http->read_ready(config_.trigger_mode % 2);
                apply_result(id, result);
            }
        }
        expire_clients();
    }
    thread_pool_.shutdown();
    drain_completions();
    clients_.clear();
    timers_.clear();
    Logger::instance().info("Server stopped; workers joined and connections released");
}
