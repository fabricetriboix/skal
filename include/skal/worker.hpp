/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/msg.hpp>
#include <skal/global.hpp>
#include <skal/queue.hpp>
#include <functional>
#include <memory>
#include <map>
#include <set>
#include <stdexcept>
#include <boost/noncopyable.hpp>
#include <boost/optional.hpp>

namespace skal {

class msg_t;

/** Send the `msg` to its recipient
 *
 * The recipient could be in this process or not. If the recipient is not in
 * this process, the message will be forwarded to skald; if this process is
 * standalone, the message is dropped.
 *
 * \param msg [in] Message to send
 */
void send(std::unique_ptr<msg_t> msg);

/** Worker class
 *
 * This class manages a worker, which is essentially a message queue paired
 * with a message processing function.
 */
class worker_t final : boost::noncopyable
{
public :
    worker_t() = delete;
    ~worker_t();

    /** Type of a functor to process a message
     *
     * When a worker is created, a companion message queue is created for it to
     * buffer messages the worker has to process. The skal framework will call
     * the `process_t` functor when messages are pushed into the queue. If
     * your process functor does not work fast enough, some backup may occur.
     * The skal framework will repeatedly call your process functor as quickly
     * as possible until the message queue is empty.
     *
     * If you want to terminate the worker, this functor should return `false`
     * and the worker will be terminated with immediate effect.
     *
     * \note This functor should not block and should return as quickly as
     *       possible; otherwise it will block the executor it is running on,
     *       and all other workers running on that executor will be blocked.
     *
     * \param msg [in] Message to process
     *
     * \return `true` to continue processing messages, `false` to terminate
     *         this worker
     *
     * \throw If this functor throws an exception, the worker will be terminated
     *        as if it returned `false`. In addition, an alarm will be raised.
     */
    typedef std::function<bool(std::unique_ptr<msg_t> msg)> process_t;

    /** Factory function to create a worker
     *
     * This will also register the worker into the global register.
     *
     * \param name            [in] Worker's name; must not be empty; must be
     *                             unique for this process
     * \param process         [in] Message processing functor for this worker;
     *                             must not be empty
     * \param queue_threshold [in] If the number of messages in this worker's
     *                             message queue reaches this threshold,
     *                             throttling will occur; please refer to TODO
     *                             for more information; this is a fine-tuning
     *                             parameter, do not touch unless you know what
     *                             you are doing.
     * \param xoff_timeout    [in] How long to wait before retrying to send;
     *                             this is a fine-tuning parameter, do not touch
     *                             unless you know what you are doing.
     *
     * \return The created worker, this function never returns an empty pointer
     *
     * \throw `duplicate_error` if a worker already exists in this process with
     *        the same name
     */
    static std::unique_ptr<worker_t> create(std::string name, process_t process,
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
     * \return `true` if OK, `false` if there is no worker in this process that
     *         can receive this message
     */
    static bool post(std::unique_ptr<msg_t>& msg);

    /** Drop the `msg`, notifying the sender if required
     *
     * \param msg [in] Message to drop
     */
    static void drop(std::unique_ptr<msg_t> msg);

    const std::string& name() const
    {
        return name_;
    }

    /** Pop the top message and process it
     *
     * \return `true` if OK, `false` if the worker must terminate ASAP
     *
     * \throw `std::underflow_error` if there are no message to process
     */
    bool process_one();

    size_t internal_msg_count() const
    {
        return queue_.internal_size();
    }

    size_t msg_count() const
    {
        return queue_.size();
    }

    bool blocked() const
    {
        return !xoff_.empty();
    }

    void listen(queue_t::ntf_t ntf)
    {
        queue_.listen(std::move(ntf));
    }

private :
    typedef std::chrono::steady_clock::time_point time_point_t;

    std::string name_;
    process_t process_;
    queue_t queue_;
    std::chrono::nanoseconds xoff_timeout_;

    /** Workers that sent me an xoff msg
     *
     * The key is the peer worker name, the value is the last time I sent
     * the peer a "skal-ntf-xon" message.
     *
     * While this map is not empty, this worker is throttled (i.e. it is not
     * allowed to process any non-internal message).
     */
    std::map<std::string, time_point_t> xoff_;

    /** Workers I sent an xoff msg to
     *
     * I will need to send them an xon msg when my queue is not full anymore.
     */
    std::set<std::string> ntf_xon_;

    worker_t(std::string name, process_t process, int64_t queue_threshold,
            std::chrono::nanoseconds xoff_timeout)
        : name_(full_name(name))
        , process_(process)
        , queue_(queue_threshold)
        , xoff_timeout_(xoff_timeout)
    {
    }

    /** Process an internal message
     *
     * \param msg [in] Message to process
     *
     * \return `true` if OK, `false` to stop this worker immediately
     */
    bool process_internal_msg(std::unique_ptr<msg_t> msg);

    /** Send "skal-xon" messages to workers which are waiting for me */
    void send_xon();

    /** Send "skal-ntf-xon" messages to workers which are blocking me */
    void send_ntf_xon();

    friend class scheduler_t;
    friend class executor_t;
};

} // namespace skal
