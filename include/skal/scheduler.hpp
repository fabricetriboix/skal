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
 *  - Own workers: it's the scheduler responsibility to manage worker objects
 *  - Select the next worker to run
 */
class scheduler_t : boost::noncopyable
{
public :
    virtual ~scheduler_t() = default;

    /** Add a worker
     *
     * \param worker [in] Worker to add
     */
    void add_worker(std::unique_ptr<worker_t> worker);

    /** Run the next worker
     *
     * \return `true` if there are more workers to run, `false` if there are
     *         no more workers to run.
     */
    bool run_one();

private :
    /** Add the worker to your internal data structures
     *
     * If a worker with the same name already exists, you must assert.
     *
     * The mutex has been locked when this function is called, so you can
     * access your internal data structures for this scheduler safely.
     *
     * \param worker [in] Worker to add
     */
    virtual void add(std::unique_ptr<worker_t> worker) = 0;

    /** Remove the given worker from your internal data structures
     *
     * The mutex has been locked when this function is called, so you can
     * access your internal data structures for this scheduler safely.
     *
     * \param worker_name [in] Full name of worker to remove
     */
    virtual void erase(const std::string& worker_name) = 0;

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

std::unique_ptr<policy_interface_t> create_policy(policy_t policy);

} // namespace skal
