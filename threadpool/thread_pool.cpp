#include "thread_pool.h"
#include "../log/logger.h"
#include <mysql/mysql.h>
#include <stdexcept>
#include <utility>

ThreadPool::ThreadPool(std::size_t thread_count) : stopping_(false)
{
    if (!thread_count) throw std::invalid_argument("Thread count must be positive");
    try {
        for (std::size_t i = 0; i < thread_count; ++i)
            workers_.emplace_back(&ThreadPool::worker_loop, this);
    } catch (...) { shutdown(); throw; }
}
void ThreadPool::worker_loop()
{
    mysql_thread_init();
    while (true)
    {
        std::function<void()> task;
        {
            std::unique_lock<std::mutex> lock(task_mutex_);
            condition_.wait(lock, [this] { return stopping_ || !tasks_.empty(); });
            if (stopping_ && tasks_.empty()) break;
            task = std::move(tasks_.front());
            tasks_.pop();
        }
        try { task(); }
        catch (const std::exception &e) { Logger::instance().error(e.what()); }
        catch (...) { Logger::instance().error("Worker task failed"); }
    }
    mysql_thread_end();
}
bool ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        if (stopping_ || tasks_.size() >= 10000) return false;
        tasks_.push(std::move(task));
    }
    condition_.notify_one();
    return true;
}
void ThreadPool::shutdown()
{
    { std::lock_guard<std::mutex> lock(task_mutex_); stopping_ = true; }
    condition_.notify_all();
    for (auto &worker : workers_) if (worker.joinable()) worker.join();
}
ThreadPool::~ThreadPool() { shutdown(); }
