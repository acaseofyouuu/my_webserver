#include "thread_pool.h"

#include <utility>

ThreadPool::ThreadPool(std::size_t thread_count)
    : stopping_(false)
{
    for (std::size_t i = 0; i < thread_count; i++)
    {
        workers_.emplace_back(&ThreadPool::worker_loop, this);
    }
}

void ThreadPool::worker_loop()
{
    while (true)
    {
        std::function<void()> task;

        {
            std::unique_lock<std::mutex> lock(task_mutex_);

            condition_.wait(lock, [this]
                            { return stopping_ || !tasks_.empty(); });

            if (stopping_ && tasks_.empty())
            {
                return;
            }

            task = std::move(tasks_.front());
            tasks_.pop();
        }

        task();
    }
}

void ThreadPool::enqueue(std::function<void()> task)
{
    {
        std::lock_guard<std::mutex> lock(task_mutex_);

        if (stopping_)
        {
            return;
        }

        tasks_.push(std::move(task));
    }

    condition_.notify_one();
}

ThreadPool::~ThreadPool()
{
    {
        std::lock_guard<std::mutex> lock(task_mutex_);
        stopping_ = true;
    }

    condition_.notify_all();

    for (std::thread &worker : workers_)
    {
        if (worker.joinable())
        {
            worker.join();
        }
    }
}