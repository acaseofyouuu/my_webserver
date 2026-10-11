#include "logger.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>
#include <system_error>
#include <thread>

Logger &Logger::instance()
{
    static Logger logger;
    return logger;
}

bool Logger::initialize(const std::string &file_path)
{
    std::lock_guard<std::mutex> lock(log_mutex_);

    std::filesystem::path path(file_path);
    std::filesystem::path directory = path.parent_path();

    if (!directory.empty())
    {
        std::error_code error;
        std::filesystem::create_directories(directory, error);

        if (error)
        {
            std::cerr << "Failed to create log directory\n";
            return false;
        }
    }

    log_file_.open(file_path, std::ios::app);

    if (!log_file_.is_open())
    {
        std::cerr << "Failed to open log file\n";
        return false;
    }

    return true;
}

void Logger::info(const std::string &message)
{
    write("INFO", message);
}

void Logger::error(const std::string &message)
{
    write("ERROR", message);
}

void Logger::write(const std::string &level,
                   const std::string &message)
{
    auto now = std::chrono::system_clock::now();
    std::time_t current_time =
        std::chrono::system_clock::to_time_t(now);

    std::tm local_time{};
    localtime_r(&current_time, &local_time);

    std::ostringstream entry;

    entry << std::put_time(&local_time, "%Y-%m-%d %H:%M:%S")
          << " [" << level << "]"
          << " [thread " << std::this_thread::get_id() << "] "
          << message;

    std::lock_guard<std::mutex> lock(log_mutex_);

    if (level == "ERROR")
    {
        std::cerr << entry.str() << '\n';
    }
    else
    {
        std::cout << entry.str() << '\n';
    }

    if (log_file_.is_open())
    {
        log_file_ << entry.str() << '\n';
        log_file_.flush();
    }
}