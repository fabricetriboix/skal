/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/msg.hpp>
#include <utility>
#include <functional>
#include <list>
#include <boost/noncopyable.hpp>

namespace skal {

/** Message queue
 *
 * This queue contains 3 types of messages ordered by priority: regular
 * messages, urgent messages and internal messages. Internal messages are for
 * the skal framework internal communications and are not directly available to
 * the client software.
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
     * This method always succeeds. If a notification functor has been
     * registered, it will be called.
     *
     * \param msg [in] Message to push; must not be an empty pointer
     */
    void push(msg_ptr_t msg);

    /** Pop a message from the queue
     *
     * If the `internal_only` argument is set, urgent and regular messages are
     * ignored, and only internal messages are popped. If there are no internal
     * messages, this method returns an empty pointer regardless of whether or
     * not urgent or regular messages are available.
     *
     * Messages are popped in the following order:
     *  - Internal messages first
     *  - If there are no internal message pending, urgent messages
     *  - Otherwise, regular messages
     *
     * \param internal_only [in] Whether to pop internal messages only
     *
     * \return The popped message, or an empty pointer if no message to pop
     */
    msg_ptr_t pop(bool internal_only = false);

    /** Get the number of pending messages
     *
     * \return The number of pending messages
     */
    size_t size() const
    {
        return internal_.size() + urgent_.size() + regular_.size();
    }

    bool is_empty() const
    {
        return size() == 0;
    }

    /** Check if the queue is full
     *
     * The queue is full if there are more than `threshold` messages.
     */
    bool is_full() const
    {
        return size() >= threshold_;
    }

    /** Check if the queue is half-full */
    bool is_half_full() const
    {
        return size() >= (threshold_ / 2);
    }

private :
    size_t threshold_;
    ntf_t ntf_;
    std::list<msg_ptr_t> internal_;
    std::list<msg_ptr_t> urgent_;
    std::list<msg_ptr_t> regular_;
};

} // namespace skal
