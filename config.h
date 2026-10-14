#pragma once

#include <cstddef>
#include <string>

class Config
{
public:
    bool parse(int argc, char *argv[]);

    int port = 8080;
    std::size_t thread_count = 4;
    int idle_timeout_seconds = 10;
    int log_mode = 0;      // -l: 0 同步，1 异步
    int trigger_mode = 0;  // -m: 0 LT/LT，1 LT/ET，2 ET/LT，3 ET/ET
    int linger_mode = 0;   // -o: SO_LINGER
    int close_log = 0;     // -c: 1 禁用日志
    int actor_model = 0;   // -a: 0 模拟 Proactor，1 Reactor
    bool help = false;

    std::string database_host = "localhost";
    std::string database_user = "webserver";
    std::string database_name = "qgydb";
    unsigned int database_port = 3306;
    std::size_t database_pool_size = 4;
};
