/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/worker.hpp>
#include <memory>
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
    virtual ~scheduler_t() = default;

    /** Add a worker
     *
     * If a worker with the same name already exists, you must assert.
     *
     * \param worker [in] Worker to add; must not be empty
     */
    virtual void add(std::unique_ptr<worker_t> worker) = 0;

    /** Remove the given worker from your internal data structures
     *
     * If the given worker does not exists, this function must do nothing.
     *
     * \param worker_name [in] Full name of worker to remove
     */
    virtual void remove(const std::string& worker_name) = 0;

    /** Select the next worker to run
     *
     * This function must inspect the workers stored in your internal data
     * structures and return the one which should be run next. If there are no
     * workers to run, return `nullptr`.
     *
     * \return A raw pointer to the worker to run, or `nullptr` if there are no
     *         workers to run
     */
    virtual worker_t* select() = 0;
};

std::unique_ptr<scheduler_t> create_scheduler(policy_t policy);

} // namespace skal
