/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/worker.hpp>
#include <skal/error.hpp>
#include <memory>
#include <boost/noncopyable.hpp>

namespace skal {

/** How to schedule workers when there is contention */
enum class policy_t {
    fair,     /**< Worker with most pending messages first */
    carousel, /**< Each in turn */
    priority, /**< Higher priority first */
};

/** Interface for a scheduling policy
 *
 * Any implementation of this interface must be MT-safe. The `add()` and the
 * `run_one()` functions will be called from different threads, potentially
 * simultaneously.
 */
class policy_interface_t : boost::noncopyable
{
public :
    virtual ~policy_interface_t() = default;

    /** Add a worker
     *
     * You should atomically add this worker to your internal structures and to
     * the global worker register.
     *
     * \param worker [in] Worker to add
     *
     * \throw `duplicate_error` if a worker with the same name already exist
     *        for this process
     */
    virtual void add(std::unique_ptr<worker_t> worker) = 0;

    /** Run the next worker
     *
     * You should select the next worker to run and run it. If its message
     * processing functor returns `false`, it must be removed immediately and
     * atomically from both your internal structures and the global worker
     * register.
     *
     * If there is no pending worker, this function should return `false`.
     *
     * \return `true` if there are more workers to run, `false` if there are
     *         no more workers to run.
     */
    virtual bool run_one() = 0;
};

std::unique_ptr<policy_interface_t> create_policy(policy_t policy);

} // namespace skal
