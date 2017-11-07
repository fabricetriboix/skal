/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/alarm.hpp>
#include <skal/blob.hpp>
#include <skal/msg.hpp>
#include <skal/worker.hpp>
#include <vector>

namespace skal {

/** How to schedule workers when there is contention */
enum class policy_t {
    biggest,  /**< Worker with most pending messages first */
    biggest,  /**< Worker with most pending messages first */
    carousel, /**< Each in turn */
    priority, /**< Higher priority first */
};

/** Executor configuration */
struct executor_t
{
    policy_t policy;
};

/** Parameters of the skal framework */
struct params_t
{
    /** URL to connect to skald; use empty string for default */
    std::string skald_url;
};

/** Run the skal framework
 *
 * The skal framework will create as many executors as indicated by the size
 * of the `exec_cfgs` vector.
 *
 * This function blocks until the skal framework is ordered to shut down by
 * a call to `terminate_skal()`.
 *
 * \param params    [in] Parameters of the skal framework
 * \param workers   [in] Initial workers; must not be empty
 * \param executors [in] Executor configurations; use empty vector for default
 *
 * \throw `bad_url` if `params.skald_url` is malformatted
 */
void run_skal(const params_t& params, std::vector<executor_t> executors,
        std::vector<worker_t> workers);

/** Terminate the skal framework
 *
 * This function will cause the skal framework to gracefully shut down. Once
 * all the workers have terminated, the `run_skal()` function will return.
 *
 * Please note this function returns immediately and before the skal framework
 * is actually shut down.
 */
void terminate_skal();

} // namespace skal
