/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/scheduler.hpp>
#include <skal/semaphore.hpp>
#include <vector>
#include <thread>
#include <mutex>
#include <atomic>
#include <boost/noncopyable.hpp>
#include <boost/asio.hpp>

namespace skal {

/** Executor class
 *
 * This is a toy implementation; real implementation to come when I have
 * numa stuff.
 */
class executor_t final : boost::noncopyable
{
public :
    executor_t(std::unique_ptr<scheduler_t> scheduler);
    ~executor_t();

    scheduler_t& scheduler()
    {
        return *scheduler_.get();
    }

private :
    std::atomic<bool> is_terminated_ = false;
    std::unique_ptr<scheduler_t> scheduler_;
    boost::asio::io_service io_service_;
    boost::asio::io_service::work work_;
    std::vector<std::thread> threads_;
    std::thread dispatcher_thread_;
    ft::semaphore_t semaphore_;
    std::mutex mutex_;
    typedef std::unique_lock<std::mutex> lock_t;

    void run_dispatcher();
};

} // namespace skal
