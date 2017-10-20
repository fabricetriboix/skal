/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include "skal-cfg.hpp"
#include "skal-error.hpp"
#include "skal-msg.hpp"
#include <utility>
#include <functional>
#include <mutex>
#include <condition_variable>
#include <list>
#include <utility>
#include <boost/noncopyable.hpp>

namespace skal {

/** Message queue
 *
 * This queue contains 3 types of messages ordered by priority: regular
 * messages, urgent messages and internal messages. Internal messages are for
 * the skal framework internal communications and are not directly available to
 * the client software.
 *
 * A number of assumptions have been made in the design of this message queue,
 * which is tailored for a specific usage. In essence, this queue is meant to
 * hold messages to be delivered to only one thread; this means that only that
 * thread will ever pop messages from it.
 *
 * In addition, it is assumed the queue is created before the thread and
 * destroyed after it.
 *
 * Finally, it is assumed that whomever pushes messages into the queue has the
 * knowledge that the thread had terminated or not. Therefore, the pusher will
 * not attempt to push into the queue once the queue's thread had terminated.
 *
 * The consequence of all these assumptions is that the queue destructor does
 * not need to perform any checks are be careful about anything, because when
 * the queue is destroyed, it is not used by either a pusher thread or the
 * recipient thread.
 */
class queue_t final : boost::noncopyable
{
public :
    queue_t() = delete;
    ~queue_t() = default;

    /** Prototype of a push notification functor */
    typedef std::function<void()> ntf_t;

    /** Constructor with push notification
     *
     * \param threshold [in] Queue threshold; must be >0
     * \param ntf       [in] Functor to call back when an item is pushed into
     *                       the queue
     */
    queue_t(size_t threshold, ntf_t ntf) : threshold_(threshold), ntf_(ntf)
    {
    }

    /** Constructor without push notification
     *
     * \param threshold [in] Queue threshold; must be >0
     */
    explicit queue_t(size_t threshold) : threshold_(threshold)
    {
    }

    /** Push a message into the queue
     *
     * This method always succeeds.
     *
     * \param msg [in] Message to push; must not be an empty pointer
     */
    void push(std::unique_ptr<msg_t> msg);

    /** Pop a message from the queue
     *
     * This is a blocking method. There are only a few blocking methods in
     * the skal framework, and this is the main one. If the queue is not empty,
     * it will pop out the message at the front of the queue. Otherwise, it
     * will block until a message is pushed into the queue.
     *
     * If the `internal_only` argument is set, urgent and regular messages are
     * ignored, and only internal messages are popped. If there are no internal
     * messages, this method blocks regardless of whether or not urgent or
     * regular messages are available.
     *
     * Messages are popped in the following order:
     *  - Internal messages first
     *  - If there are no internal message pending, urgent messages
     *  - Otherwise, regular messages
     *
     * \param internal_only [in] Whether to pop internal messages only
     *
     * \return The popped message, never an empty pointer
     */
    std::unique_ptr<msg_t> pop_BLOCKING(bool internal_only = false);

    /** Pop a message from the queue, non-blocking version
     *
     * \param internal_only [in] Whether to pop internal messages only
     *
     * \return The popped message, or an empty pointer if no message to pop
     */
    std::unique_ptr<msg_t> pop(bool internal_only = false);

    /** Get the number of pending messages
     *
     * \return The number of pending messages
     */
    size_t size() const;

    bool empty() const
    {
        return size() == 0;
    }

    /** Check if the queue is full
     *
     * The queue is full if there are more than `threshold` messages.
     */
    bool is_full() const;

    /** Check if the queue is half-full */
    bool is_half_full() const;

private :
    size_t threshold_;
    ntf_t ntf_;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    std::list<std::unique_ptr<msg_t>> internal_;
    std::list<std::unique_ptr<msg_t>> urgent_;
    std::list<std::unique_ptr<msg_t>> regular_;

    typedef std::unique_lock<std::mutex> lock_t;
};

} // namespace skal
