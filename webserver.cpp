#include "webserver.h"
#include "http/http_conn.h"

#include <iostream>
#include <sys/socket.h>
#include <unistd.h>
#include <netinet/in.h>
#include <sys/epoll.h>

void WebServer::mark_client_idle(int client_fd)
{
    std::lock_guard<std::mutex> lock(client_activity_mutex_);

    client_last_active_[client_fd] =
        std::chrono::steady_clock::now();
}

void WebServer::remove_client_from_idle(int client_fd)
{
    std::lock_guard<std::mutex> lock(client_activity_mutex_);

    client_last_active_.erase(client_fd);
}

void WebServer::close_idle_connections()
{
    constexpr auto idle_timeout = std::chrono::seconds(10);
    auto now = std::chrono::steady_clock::now();

    std::lock_guard<std::mutex> lock(client_activity_mutex_);

    for (auto iterator = client_last_active_.begin();
         iterator != client_last_active_.end();)
    {
        auto idle_time = now - iterator->second;

        if (idle_time >= idle_timeout)
        {
            int client_fd = iterator->first;

            epoll_ctl(epoll_fd_,
                      EPOLL_CTL_DEL,
                      client_fd,
                      nullptr);

            close(client_fd);

            std::cout << "Closed idle client, fd = "
                      << client_fd << '\n';

            iterator = client_last_active_.erase(iterator);
        }
        else
        {
            ++iterator;
        }
    }
}

WebServer::WebServer(int port)
    : port_(port),
      server_fd_(-1),
      epoll_fd_(-1),
      thread_pool_(4)
{
}

WebServer::~WebServer()
{
    if (epoll_fd_ != -1)
    {
        close(epoll_fd_);
    }

    if (server_fd_ != -1)
    {
        close(server_fd_);
    }
}

bool WebServer::start()
{
    server_fd_ = socket(AF_INET, SOCK_STREAM, 0);

    if (server_fd_ == -1)
    {
        std::cerr << "Failed to create socket\n";
        return false;
    }

    std::cout << "Socket created successfully , fd = " << server_fd_ << '\n';

    int reuse_address = 1;

    if (setsockopt(server_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse_address, sizeof(reuse_address)) == -1)
    {
        std::cerr << "Failed to set socket options\n";
        return false;
    }

    sockaddr_in server_address{};

    server_address.sin_family = AF_INET;
    server_address.sin_addr.s_addr = htonl(INADDR_ANY);
    server_address.sin_port = htons(port_);

    if (bind(server_fd_, reinterpret_cast<sockaddr *>(&server_address), sizeof(server_address)) == -1)
    {
        std::cerr << "Failed to bind socket\n";
        return false;
    }

    std::cout << "Socket bound to port " << port_ << '\n';

    if (listen(server_fd_, 10) == -1)
    {
        std::cerr << "Failed to listen on socket\n";
        return false;
    }

    std::cout << "Server is listening on port " << port_ << '\n';

    epoll_fd_ = epoll_create1(0);

    if (epoll_fd_ == -1)
    {
        std::cerr << "Failed to create epoll instance\n";
        return false;
    }

    epoll_event server_event{};

    server_event.events = EPOLLIN;
    server_event.data.fd = server_fd_;

    if (epoll_ctl(epoll_fd_,
                  EPOLL_CTL_ADD,
                  server_fd_,
                  &server_event) == -1)
    {
        std::cerr << "Failed to add server socket to epoll\n";
        return false;
    }

    return true;
}

void WebServer::run()
{
    constexpr int max_events = 64;
    epoll_event events[max_events]{};

    while (true)
    {
        std::cout << "Waiting for events..." << std::endl;

        int event_count =
            epoll_wait(epoll_fd_, events, max_events, 1000);

        if (event_count == -1)
        {
            std::cerr << "Failed to wait for epoll events\n";
            return;
        }

        for (int i = 0; i < event_count; ++i)
        {
            int ready_fd = events[i].data.fd;

            if (ready_fd == server_fd_)
            {
                int client_fd = accept(server_fd_, nullptr, nullptr);

                if (client_fd == -1)
                {
                    std::cerr << "Failed to accept client\n";
                    continue;
                }

                epoll_event client_event{};

                client_event.events = EPOLLIN;
                client_event.data.fd = client_fd;

                if (epoll_ctl(epoll_fd_,
                              EPOLL_CTL_ADD,
                              client_fd,
                              &client_event) == -1)
                {
                    std::cerr << "Failed to add client socket to epoll\n";
                    close(client_fd);
                    continue;
                }

                mark_client_idle(client_fd);

                std::cout << "Client connected and added to epoll, fd = "
                          << client_fd << '\n';
            }
            else
            {
                remove_client_from_idle(ready_fd);

                if (epoll_ctl(epoll_fd_,
                              EPOLL_CTL_DEL,
                              ready_fd,
                              nullptr) == -1)
                {
                    std::cerr << "Failed to remove client socket from epoll\n";
                    close(ready_fd);
                    continue;
                }

                thread_pool_.enqueue([this, ready_fd]
                                     {
                    HttpConnection connection(ready_fd);

                    bool keep_alive = connection.handle();

                    if(!keep_alive){
                        close(ready_fd);
                        return;
                    }

                    mark_client_idle(ready_fd);

                    epoll_event client_event{};
                    client_event.events = EPOLLIN;
                    client_event.data.fd = ready_fd;

                    if(epoll_ctl(epoll_fd_,
                                 EPOLL_CTL_ADD,
                                 ready_fd,
                                 &client_event) == -1)
                    {
                        std::cerr << "Failed to add keep_alive connection back to epoll\n";
                        remove_client_from_idle(ready_fd);
                        close(ready_fd);
                    } });
            }
        }

        close_idle_connections();
    }
}