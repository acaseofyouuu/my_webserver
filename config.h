#pragma once

#include <cstddef>

class Config
{
public:
    bool parse(int argc, char *argv[]);

    int port = 8080;
    std::size_t thread_count = 4;
    int idle_timeout_seconds = 10;
};