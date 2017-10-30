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

/** Executor configuration */
struct exec_cfg_t
{
    /** CPU number to run this executor on
     *
     * The first CPU is number 0, 2nd CPU number 1, etc.
     */
    size_t cpu;
};

/** Run the skal framework
 *
 * The skal framework will create as many executors as indicated by the size
 * of the `exec_cfgs` vector.
 *
 * This function blocks until the skal framework is ordered to shut down by
 * a call to `terminate_skal()`.
 *
 * \param skald_url    [in] URL to connect to skald; use empty string for
 *                          default
 * \param workers      [in] Initial workers; must not be empty
 * \param exec_cfgs    [in] Executor configurations; use empty vector for
 *                          default
 *
 * \throw `bad_url` if `skald_url` is malformatted
 */
void run_skal(std::string skald_url,
        std::vector<exec_cfg_t> exec_cfgs,
        std::vector<worker_params_t> workers);

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
