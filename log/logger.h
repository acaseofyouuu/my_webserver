#pragma once
#include <condition_variable>
#include <fstream>
#include <mutex>
#include <queue>
#include <string>
#include <thread>

class Logger
{
public:
    static Logger &instance();
    bool initialize(const std::string &path, bool asynchronous = false, bool enabled = true,
                    std::size_t split_lines = 5000000);
    void info(const std::string &message);
    void error(const std::string &message);
    void shutdown();
private:
    struct Entry { std::string text, day; bool error; };
    Logger() = default;
    ~Logger();
    Logger(const Logger &) = delete;
    Logger &operator=(const Logger &) = delete;
    void write(const std::string &level, const std::string &message);
    void sink(const Entry &entry);
    void worker();
    std::ofstream file_;
    std::string path_, day_;
    std::size_t lines_ = 0, split_lines_ = 5000000, part_ = 0;
    bool enabled_ = false, asynchronous_ = false, stopping_ = false;
    std::mutex mutex_;
    std::condition_variable available_, space_;
    std::queue<Entry> queue_;
    std::thread worker_;
};
