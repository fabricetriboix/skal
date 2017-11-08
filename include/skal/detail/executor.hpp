/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/skal.hpp>
#include <internal/semaphore.hpp>
#include <internal/job.hpp>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <utility>
#include <boost/noncopyable.hpp>

namespace skal {

/** Executor class */
class executor_t final : boost::noncopyable
{
    typedef std::list<std::unique_ptr<job_t>> jobs_t;

    typedef std::unique_lock<std::mutex> lock_t;
    mutable std::mutex mutex_; /**< Mutex to protect `workers_to_add_` */
    std::list<worker_t> workers_to_add_;

    ft::semaphore_t semaphore_;
    // TODO std::unique_ptr<policy_interface_t> policy_;
    std::thread thread_;
    jobs_t jobs_;

    /** Thread entry point */
    void run();

    /** Create a job for the given worker */
    void create_job(worker_t worker);

    /** Pop a message for the given job and process it */
    void run_one(jobs_t::iterator& job);

public :
    executor_t() = delete;
    ~executor_t();

    /** Constructor
     *
     * \param policy [in] Type of policy to use
     */
    explicit executor_t(policy_t policy);

    /** Add a worker to be executed on this executor
     *
     * \param worker [in] Worker to add
     */
    void add(worker_t worker)
    {
        lock_t lock(mutex_);
        workers_to_add_.push_back(std::move(worker));
        semaphore_.post();
    }
};

} // namespace skal
