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

    std::string database_host = "localhost";
    std::string database_user = "webserver";
    std::string database_name = "qgydb";
    unsigned int database_port = 3306;
    std::size_t database_pool_size = 4;
};