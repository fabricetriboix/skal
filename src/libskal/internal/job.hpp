/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/worker.hpp>
#include <internal/queue.hpp>
#include <memory>
#include <map>
#include <set>
#include <chrono>
#include <mutex>
#include <utility>
#include <boost/noncopyable.hpp>

namespace skal {

/** Job type
 *
 * A job type is in essence a combination of a message queue with a message
 * processing function.
 */
struct job_t final : boost::noncopyable
{
    job_t() = delete;
    ~job_t() = default;

    int priority;
    queue_t queue;
    process_msg_t process_msg;

    /** Workers that told me to stop sending to them
     *
     * The key is the worker's name, and the value the last time we sent a
     * "skal-ntf-xon" to that worker.
     */
    std::map<std::string, std::chrono::steady_clock::time_point> xoff;

    /** Names of workers to notify when they can start sending again */
    std::set<std::string> ntf_xon;

    job_t(const worker_t& worker, queue_t::ntf_t ntf)
        : priority(worker.priority)
        , queue(worker.queue_threshold, ntf)
        , process_msg(worker.process_msg)
    {
    }

    class lock_t final
    {
        std::unique_lock<std::mutex> lock_;
    public :
        lock_t();
        ~lock_t() = default;
        lock_t(lock_t&& right) : lock_(std::move(right.lock_)) { }
    };

    /** Lock the global job data structure
     *
     * You must acquire such a lock before calling `add()`, `remove()` or
     * `lookup()`.
     */
    static lock_t get_lock();

    /** Add a job
     *
     * You must have locked the global structure using `get_lock()` beforehand.
     *
     * \param worker_name [in] Name of worker of job to add
     * \param job         [in] Pointer to job to add, the caller owns the job
     *                         object; must not be `nullptr`
     *
     * \throw `duplicate_error` if a job already exists for that worker
     */
    static void add(const std::string& worker_name, job_t* job);

    /** Remove a job
     *
     * You must have locked the global structure using `get_lock()` beforehand.
     *
     * If there is no job for the given worker, this method does nothing.
     *
     * \param worker_name [in] Name of worker whose job to remove
     */
    static void remove(const std::string& worker_name);

    /** Lookup a job from the name of its worker
     *
     * You must have locked the global structure using `get_lock()` beforehand.
     *
     * \return The found job, or `nullptr` if no job found
     */
    static job_t* lookup(const std::string& worker_nane);
};

} // namespace skal
