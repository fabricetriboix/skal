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

/** Parameters of the skal framework */
struct parameters_t
{
    /** Whether to connect to a skald or not */
    bool standalone = true;

    /** URL to connect to skald; use empty string for default
     *
     * Ignored if `standalone` is `true`.
     */
    std::string skald_url = std::string();
};

/** Initialise the skal framework
 *
 * This must be the first function you call.
 *
 * \param parameters [in] Skal parameters
 */
void init(const parameters_t& parameters);

/** Run skal until all workers are finished
 *
 * You must call this function once you created your initial workers. If you
 * have not created any worker, this function returns immediately. Please note
 * that no worker will run until this function is called.
 */
void wait();

/** Terminate the skal framework
 *
 * This function will cause the skal framework to gracefully shut down. Once
 * all the workers have terminated, the `wait()` function will return.
 *
 * Please note this function returns immediately and before the skal framework
 * is actually shut down.
 */
void terminate();

} // namespace skal
