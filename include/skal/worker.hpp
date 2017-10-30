/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/msg.hpp>
#include <skal/error.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <boost/noncopyable.hpp>

namespace skal {

/** Exception: this worker is finished */
struct worker_done : public error
{
    worker_done() : error("skal::worker_done") { }
};

/** Type of a functor to process a message
 *
 * When a worker is created, a companion skal-queue is created for it to buffer
 * messages the worker has to process. The skal framework will call the
 * `process_t` functor when messages are pushed into the queue. If your process
 * functor does not work fast enough, some backup may occur. The skal framework
 * will repeatidly call your process functor as quickly as possible until the
 * message queue is empty.
 *
 * If you want to terminate the worker, this functor should throw an exception.
 * That would typically be `worker_done`, but any exception will terminate the
 * worker with immediate effect.
 *
 * \note This functor is not allowed to block, except for when mapping a blob.
 *
 * \param msg [in] Message to process
 *
 * \throw This functor may throw any exception. The worker will be immediately
 *        terminated if an exception is caught. The `worker_done` exception is
 *        provided for terminating the worker when you have no good exception
 *        to throw (eg: the worker just finished its job and wants to terminate
 *        cleanly).
 */
typedef std::function<void(std::unique_ptr<msg_t> msg)> processor_t;

/** Parameters for the worker */
struct worker_params_t
{
    std::string name;
    processor_t processor;

    /** Message queue threshold for this worker; use 0 for skal default
     *
     * If the number of messages in the worker's queue reaches this threshold,
     * throttling will occur; please refer to TODO for more information.
     */
    int64_t queue_threshold = 0;

    int executor = -1;

    /** How long to wait before retrying to send; use 0 for skal default
     *
     * If blocked by another worker for `xoff_timeout`, the skal framework will
     * send that worker a "skal-ntf-xon" message to tell it to inform us
     * whether we can send again.
     *
     * This is a fine-tuning parameter. Set it to a non-default value only if
     * you know what you are doing.
     */
    std::chrono::microseconds xoff_timeout = std::chrono::microseconds(0);

    // TODO: stats
};

/** Create a worker
 *
 * \param name    [in] Worker name; must be unique within this process
 * \param process [in] Functor to process messages; must not be empty
 * \param params  [in] Worker parameters
 */
void create_worker(const worker_params_t& params);

} // namespace skal
