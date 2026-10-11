#pragma once

#include <fstream>
#include <mutex>
#include <string>

class Logger
{
public:
    static Logger &instance();

    bool initialize(const std::string &file_path);

    void info(const std::string &message);
    void error(const std::string &message);

private:
    Logger() = default;
    ~Logger() = default;

    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;

    void write(const std::string &level,
               const std::string &message);

    std::ofstream log_file_;
    std::mutex log_mutex_;
};