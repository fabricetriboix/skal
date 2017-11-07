/* Copyright Fabrice Triboix */

#pragma once

#include <detail/safe-mutex.hpp>
#include <mutex>
#include <condition_variable>
#include <chrono>

namespace ft {

class semaphore_t
{
    typedef std::unique_lock<safe_mutex<std::mutex>> lock_t;

public:
    semaphore_t() : count_(0) { }
    explicit semaphore_t(int initial_count) : count_(initial_count) { }
    ~semaphore_t() = default;

    int count()
    {
        lock_t lock(mutex_);
        return count_;
    }

    void post()
    {
        lock_t lock(mutex_);
        ++count_;
        cv_.notify_one();
    }

    void take()
    {
        lock_t lock(mutex_);
        while (count_ <= 0) {
            cv_.wait(lock);
        }
        --count_;
    }

    bool take(std::chrono::nanoseconds timeout)
    {
        auto end = std::chrono::high_resolution_clock::now() + timeout;
        lock_t lock(mutex_);
        while (count_ <= 0) {
            if (cv_.wait_until(lock, end) == std::cv_status::timeout) {
                return false;
            }
        }
        --count_;
        return true;
    }

private:
    safe_mutex<std::mutex> mutex_;
    std::condition_variable_any cv_;
    int count_;

    semaphore_t(const semaphore_t&) = delete;
    semaphore_t& operator=(const semaphore_t&) = delete;
};

} // namespace ft
