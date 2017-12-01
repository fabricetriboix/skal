/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/msg.hpp>
#include <skal/queue.hpp>
#include <functional>
#include <memory>
#include <set>
#include <thread>
#include <boost/noncopyable.hpp>

namespace skal {

/** Send the `msg` to its recipient
 *
 * The recipient could be in this process or not. If the recipient is not in
 * this process, the message will be forwarded to skald; if this process is
 * standalone, the message is dropped.
 *
 * \param msg [in] Message to send
 */
void send(std::unique_ptr<msg_t> msg);

/** Drop the `msg`
 *
 * \param msg [in] Message to drop
 */
void drop(std::unique_ptr<msg_t> msg);

/** Worker class
 *
 * This class manages a worker, which is essentially a thread with a message
 * queue paired with a message processing function.
 */
class worker_t final : boost::noncopyable
{
public :
    /** Type of a functor to process a message
     *
     * When a worker is created, a companion message queue is created for it to
     * buffer messages the worker has to process. The skal framework will call
     * the `process_msg_t` functor when messages are pushed into the queue. If
     * your process functor does not work fast enough, some backup may occur
     * and message will accumulate in the queue. The skal framework will
     * repeatedly call your process functor as quickly as possible until the
     * message queue is empty.
     *
     * If you want to terminate the worker, this functor should return `false`
     * and the worker will be terminated with immediate effect.
     *
     * \note This functor should return as quickly as possible.
     *
     * \param msg [in] Message to process
     *
     * \return `true` to continue processing messages, `false` to terminate
     *         this worker
     *
     * \throw If this functor throws an exception, the worker will be
     *        terminated as if it returned `false`. In addition, an alarm will
     *        be raised.
     */
    typedef std::function<bool(std::unique_ptr<msg_t> msg)> process_msg_t;

private :
    std::string name_;
    process_msg_t process_msg_;
    queue_t queue_;
    std::chrono::nanoseconds xoff_timeout_;
    std::thread thread_;

    /** Workers that sent me an xoff msg
     *
     * While this is not empty, this worker is throttled (i.e. it is not
     * allowed to process any non-internal message).
     */
    std::set<std::string> xoff_;

    /** When did I receive the last xoff message */
    timepoint_t last_xoff_;

    /** Workers I sent an xoff msg to
     *
     * I will need to send them an xon msg when my queue is not full anymore.
     */
    std::set<std::string> ntf_xon_;

public :
    worker_t() = delete;
    ~worker_t();

    /** Factory function to create a worker
     *
     * Please note that worker names starting with "skal" are reserved for the
     * skal framework internal usage.
     *
     * \param name            [in] Worker's name; must not be empty; must be
     *                             unique for this process; if `name` does not
     *                             contain a '@' character, the domain name of
     *                             this process will be appended
     * \param process_msg     [in] Message processing functor for this worker
     * \param numa_node       [in] NUMA node on which to run this worker;
     *                             -1 to not configure a NUMA node
     * \param queue_threshold [in] If the number of messages in this worker's
     *                             message queue reaches this threshold,
     *                             throttling will occur; please refer to the
     *                             to-level "README.md" file for more
     *                             information; this is a fine-tuning
     *                             parameter, do not touch unless you know what
     *                             you are doing.
     * \param xoff_timeout    [in] How long to wait before coming out of pause;
     *                             this is a fine-tuning parameter, do not touch
     *                             unless you know what you are doing.
     *
     * \throw `duplicate_error` if a worker already exists in this process with
     *        the same name
     */
    static void create(std::string name,
            process_msg_t process_msg,
            int numa_node = -1,
            int64_t queue_threshold = default_queue_threshold,
            std::chrono::nanoseconds xoff_timeout = default_xoff_timeout);

    /** Post a message to the given worker in this process
     *
     * The worker that will receive this message is indicated by the `msg`'s
     * recipient.
     *
     * \param msg [in] Message to post; the message will be consumed only if
     *                 this function returns `true` (in other words, if this
     *                 function returns `true`, `msg` will be empty)
     *
     * \return `true` if OK, `false` if the message's recipient is not in this
     *         process
     */
    static bool post(std::unique_ptr<msg_t>& msg);

private :
    worker_t(std::string name, process_msg_t process_msg, int numa_node,
            int64_t queue_threshold, std::chrono::nanoseconds xoff_timeout);

    /** Thread entry point */
    void run_safe();
    void run();

    /** Process an internal message
     *
     * \param msg [in] Message to process
     *
     * \return `true` if OK, `false` to stop this worker immediately
     */
    bool process_internal_msg(std::unique_ptr<msg_t> msg);

    /** Send "skal-xon" messages to workers which are waiting for me */
    void send_xon();
};

} // namespace skal
