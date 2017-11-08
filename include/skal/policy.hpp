/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/worker.hpp>
#include <memory>
#include <boost/noncopyable.hpp>

namespace skal {

/** Interface for a scheduling policy
 *
 * Any implementation of this interface must be MT-safe. The `add()` and the
 * `run_one()` methods will be called from different threads, potentially
 * simultaneously.
 */
class policy_interface_t : boost::noncopyable
{
public :
    virtual ~policy_interface_t() = default;

    /** Add a worker
     *
     * If the worker's process function returns `false`, it should be removed
     * from the policy's internal data structures.
     *
     * \param params [in] Worker's parameters
     *
     * \throw `std::invalid_argument` if `params` contains invalid values or if
     *        there is already a worker with that name
     */
    virtual void add(const worker_t& params) = 0;

    /** Run the next worker
     *
     * \return `true` if there are more workers to run, `false` if there are
     *         no more workers to run.
     */
    virtual bool run_one() = 0;
};

std::unique_ptr<policy_interface_t> create_policy(policy_t policy);

} // namespace skal
