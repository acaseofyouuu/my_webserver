#include "logger.h"
#include <chrono>
#include <ctime>
#include <filesystem>
#include <iomanip>
#include <iostream>
#include <sstream>

Logger &Logger::instance() { static Logger logger; return logger; }
Logger::~Logger() { shutdown(); }
bool Logger::initialize(const std::string &path, bool asynchronous, bool enabled, std::size_t split_lines)
{
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open() || worker_.joinable()) return false;
    enabled_ = enabled;
    asynchronous_ = asynchronous;
    stopping_ = false;
    path_ = path;
    split_lines_ = split_lines ? split_lines : 1;
    if (!enabled_) return true;
    std::error_code error;
    auto directory = std::filesystem::path(path).parent_path();
    if (!directory.empty()) std::filesystem::create_directories(directory, error);
    if (error) return false;
    file_.open(path, std::ios::app);
    if (!file_) return false;
    if (asynchronous_) worker_ = std::thread(&Logger::worker, this);
    return true;
}
void Logger::info(const std::string &message) { write("INFO", message); }
void Logger::error(const std::string &message) { write("ERROR", message); }
void Logger::write(const std::string &level, const std::string &message)
{
    auto time = std::chrono::system_clock::to_time_t(std::chrono::system_clock::now());
    std::tm local{};
    localtime_r(&time, &local);
    std::ostringstream text, day;
    day << std::put_time(&local, "%Y-%m-%d");
    text << std::put_time(&local, "%Y-%m-%d %H:%M:%S") << " [" << level << "] [thread "
         << std::this_thread::get_id() << "] ";
    for (unsigned char ch : message)
        text << ((ch < 32 || ch == 127) ? ' ' : static_cast<char>(ch));
    Entry entry{text.str(), day.str(), level == "ERROR"};
    std::unique_lock<std::mutex> lock(mutex_);
    if (!enabled_ || stopping_) return;
    if (!asynchronous_) { sink(entry); return; }
    space_.wait(lock, [this] { return stopping_ || queue_.size() < 4096; });
    if (stopping_) return;
    queue_.push(std::move(entry));
    available_.notify_one();
}
void Logger::sink(const Entry &entry)
{
    if ((!day_.empty() && day_ != entry.day) || lines_ >= split_lines_)
    {
        file_.close();
        std::error_code error;
        std::string archive;
        do { archive = path_ + "." + day_ + "." + std::to_string(++part_); }
        while (std::filesystem::exists(archive));
        std::filesystem::rename(path_, archive, error);
        if (error) std::cerr << "Log rotation failed: " << error.message() << '\n';
        file_.open(path_, std::ios::app);
        lines_ = 0;
    }
    day_ = entry.day;
    (entry.error ? std::cerr : std::cout) << entry.text << '\n';
    file_ << entry.text << '\n';
    file_.flush();
    ++lines_;
}
void Logger::worker()
{
    std::unique_lock<std::mutex> lock(mutex_);
    while (true)
    {
        available_.wait(lock, [this] { return stopping_ || !queue_.empty(); });
        if (queue_.empty() && stopping_) return;
        Entry entry = std::move(queue_.front());
        queue_.pop();
        space_.notify_all();
        lock.unlock();
        sink(entry); // 文件只由日志线程写，磁盘 I/O 不占用队列锁。
        lock.lock();
    }
}
void Logger::shutdown()
{
    { std::lock_guard<std::mutex> lock(mutex_); stopping_ = true; }
    available_.notify_all(); space_.notify_all();
    if (worker_.joinable()) worker_.join();
    std::lock_guard<std::mutex> lock(mutex_);
    if (file_.is_open()) file_.close();
}
