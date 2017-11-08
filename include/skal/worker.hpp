/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/msg.hpp>
#include <skal/error.hpp>
#include <vector>
#include <memory>
#include <functional>
#include <utility>
#include <boost/noncopyable.hpp>

namespace skal {

/** Exception: this worker is finished */
struct worker_done : public error
{
    worker_done() : error("skal::worker_done") { }
};

/** Type of a functor to process a message
 *
 * When a worker is created, a companion message queue is created for it to
 * buffer messages the worker has to process. The skal framework will call the
 * `process_msg_t` functor when messages are pushed into the queue. If your
 * process functor does not work fast enough, some backup may occur. The skal
 * framework will repeatedly call your process functor as quickly as possible
 * until the message queue is empty.
 *
 * If you want to terminate the worker, this functor should throw an exception.
 * That would typically be `worker_done`, but any exception will terminate the
 * worker with immediate effect.
 *
 * \note This functor should not block; otherwise it will block the executor
 *       it is running on, and all other workers running on that executor will
 *       be blocked.
 *
 * \param msg [in] Message to process
 *
 * \throw This functor may throw any exception. The worker will be immediately
 *        terminated if an exception is caught. The `worker_done` exception is
 *        provided for terminating the worker when its natural lifetime is
 *        finished.
 */
typedef std::function<void(std::unique_ptr<msg_t> msg)> process_msg_t;

/** Worker parameters */
struct worker_t
{
    /** Constructor: use default values wherever it makes sense
     *
     * \param name    [in] Worker's name; must not be empty
     * \param process [in] Message processing functor; must not be empty
     */
    worker_t(std::string name, process_msg_t process);

    void check() const
    {
        skal_assert(!name.empty() && process_msg && (queue_threshold > 0)
                && (xoff_timeout > std::chrono::microseconds(0)));
    }

    /** Worker's name */
    std::string name;

    /** Message processing functor
     *
     * This is the functor to call when the worker has a message to process.
     * It must not be empty.
     */
    process_msg_t process_msg;

    /** Worker priority
     *
     * This may be used by some scheduling policies.
     *
     * Typically, this number is arbitrary and is only significant relative to
     * priorities of other workers running on the same executor. Although this
     * depends on the actual scheduling policy being used.
     */
    int priority;

    /** Message queue threshold for this worker
     *
     * If the number of messages in the worker's queue reaches this threshold,
     * throttling will occur; please refer to TODO for more information.
     *
     * Must be >0.
     */
    int64_t queue_threshold;

    /** How long to wait before retrying to send
     *
     * If blocked by another worker for `xoff_timeout`, the skal framework will
     * send that worker a "skal-ntf-xon" message to tell it to inform us
     * whether we can send again.
     *
     * This is a fine-tuning parameter. Set it to a non-default value only if
     * you know what you are doing.
     *
     * Must be >0.
     */
    std::chrono::microseconds xoff_timeout;

    // TODO: stats
};

/** Create a worker
 *
 * \param worker [in] Worker's parameters
 */
void create_worker(const worker_t& worker);

} // namespace skal
