/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/worker.hpp>
#include <memory>
#include <mutex>
#include <utility>
#include <boost/noncopyable.hpp>

namespace skal {

/** Standard scheduling policies offered by skal */
enum class policy_t {
    fair,     /**< Worker with most pending messages first */
    carousel, /**< Each in turn */
    priority, /**< Higher priority first */
};

/** Scheduler base class
 *
 * A scheduler's responsibilities are:
 *  - To own workers: it's the scheduler responsibility to manage worker objects
 *  - To select the next worker to run
 *
 * This class must be derived and its pure virtual functions overriden.
 */
class scheduler_t : boost::noncopyable
{
public :
    /** Destructor
     *
     * Your derived class must remove all its workers when destructed, like if
     * `remove()` is being called on every worker it manages.
     */
    virtual ~scheduler_t() = default;

private :
    mutable std::mutex mutex_;
    typedef std::unique_lock<std::mutex> lock_t;

    /** Add a worker
     *
     * \param worker [in] Worker to add; must not be empty
     */
    void add(std::unique_ptr<worker_t> worker)
    {
        lock_t lock(mutex_);
        do_add(std::move(worker));
    }

    /** Remove the given worker from the scheduler's internal data structures
     *
     * If the given worker does not exists, this function does nothing.
     *
     * \param worker_name [in] Full name of worker to remove
     */
    void remove(const std::string& worker_name)
    {
        lock_t lock(mutex_);
        do_remove(worker_name);
    }

    bool is_empty() const
    {
        lock_t lock(mutex_);
        return do_is_empty();
    }

    /** Select the next worker to run
     *
     * \return A shared pointer to the worker to run, or an empty pointer if
     *         there are no workers to run
     */
    std::shared_ptr<worker_t> select() const
    {
        lock_t lock(mutex_);
        return do_select();
    }

private :
    /** Add a worker
     *
     * If a worker with the same name already exists, you must assert (NB:
     * the worker has already been registered to the global worker's register,
     * so it is impossible this worker has a duplicate anywhere in this
     * process).
     *
     * \param worker [in] Worker to add, will not be empty
     */
    virtual void do_add(std::unique_ptr<worker_t> worker) = 0;

    /** Remove the given worker from your internal data structures
     *
     * If the given worker does not exists, this function must do nothing.
     *
     * \param worker_name [in] Full name of worker to remove
     */
    virtual void do_remove(const std::string& worker_name) = 0;

    virtual bool do_is_empty() const = 0;

    /** Select the next worker to run
     *
     * This function must inspect the workers stored in your internal data
     * structures and return the one which should be run next. If there are no
     * workers to run, return `nullptr`.
     *
     * \return A shared pointer to the worker to run, or an empty pointer if
     *         there are no workers to run
     */
    virtual std::shared_ptr<worker_t> do_select() const = 0;

    friend class executor_t;
};

/** Create one of the standard skal scheduler
 *
 * \param policy [in] Scheduling policy to use
 *
 * \return The requested scheduler, never empty
 */
std::unique_ptr<scheduler_t> create_scheduler(policy_t policy = policy_t::fair);

} // namespace skal
