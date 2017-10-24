/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include "skal-cfg.hpp"
#include "skal-msg.hpp"
#include "skal-error.hpp"
#include <vector>
#include <memory>
#include <functional>
#include <boost/noncopyable.hpp>

namespace skal {

/** Exception: this worker is finished */
struct end_worker : public error
{
    end_worker() : error("skal::end_worker") { }
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
 * That would typically be `end_worker`, but any exception will terminate the
 * worker with immediate effect.
 *
 * \note This functor is not allowed to block, except for when mapping a blob.
 *
 * \param msg [in] Message to process
 *
 * \throw This functor may throw any exception. The worker will be immediately
 *        terminated if an exception is caught. The `end_worker` exception is
 *        provided for terminating the worker when you have no good exception
 *        to throw (eg: the worker just finished its job and wants to termiante
 *        cleanly).
 */
typedef std::function<void(std::unique_ptr<msg_t> msg)> process_t;

/** Parameters for the worker and its underlying thread */
struct worker_params_t
{
    /** Message queue threshold for this worker; use 0 for skal default
     *
     * If the number of messages in the worker's queue reaches this threshold,
     * throttling will occur; please refer to TODO for more information.
     */
    int64_t queue_threshold = 0;

    /** Stack size for the underlying thread; use 0 for OS default
     *
     * Stack size of the worker's underlying thread. You shouldn't need to set
     * this parameter, think twice before doing it.
     */
    int32_t stack_size_B = 0;

    /** CPU affinity
     *
     * This is a list of the CPUs the worker should run on. Keep it empty to
     * not set any CPU affinity. The CPUs are `int` numbers starting at 0 for
     * the first CPU, 1 for the 2nd, etc.
     */
    std::vector<int> cpus;

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

// TODO: from here
/** Initialise this module
 *
 * \param skald_url [in] URL to connect to the local skald, or the empty
 *                       string for the default
 */
void init_worker(std::string skald_url);

/** Create a worker
 *
 * \param name    [in] Worker name; must be unique within this process
 * \param process [in] Functor to process messages; must not be empty
 * \param params  [in] Worker parameters
 */
void create_worker(std::string name, process_t process,
        const worker_params_t& params);

/** Create a worker with default values for all its parameters
 *
 * \param name    [in] Worker name; must be unique within this process
 * \param process [in] Functor to process messages; must not be empty
 */
void create_worker(std::string name, process_t process);

/** Pause the execution of the calling thread
 *
 * This call blocks until either (a) all the workers have finished or (b) the
 * `cancel_pause()` function is called.
 *
 * This call can typically be used by the main thread once all the workers have
 * been created.
 *
 * \return `true` if all workers have terminated, or `false` if
 *         `cancel_pause()` has been called
 */
bool pause();

/** Cancel a `pause()`
 *
 * This can typically be used when the process needs to terminate, for example
 * when a signal such as SIGTERM as been received.
 */
void cancel_pause();

} // namespace skal
