/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/msg.hpp>
#include <mutex>
#include <condition_variable>
#include <utility>
#include <list>
#include <boost/noncopyable.hpp>

namespace skal {

/** Message queue
 *
 * This queue contains 3 types of messages ordered by priority: regular
 * messages, urgent messages and internal messages. Internal messages are for
 * the skal framework internal communications and are not directly available to
 * the client software.
 *
 * This queue is MT-safe.
 */
class queue_t final : boost::noncopyable
{
    typedef std::unique_lock<std::mutex> lock_t;
    mutable std::mutex mutex_;
    std::condition_variable cv_;
    size_t threshold_;
    std::list<std::unique_ptr<msg_t>> internal_;
    std::list<std::unique_ptr<msg_t>> urgent_;
    std::list<std::unique_ptr<msg_t>> regular_;

    size_t size_() const
    {
        return internal_.size() + urgent_.size() + regular_.size();
    }

public :
    queue_t() = delete;

    /** Destructor
     *
     * NB: Destructing a locked `std::mutex` results in undefined behaviour, so
     *     we want to avoid that. The queue will be destructed when its worker
     *     is destructed. Before the worker is destructed, it is removed from
     *     the worker register, so no messages can be sent to it anymore; so
     *     the queue's `push()` function will never be called after that point.
     *     All the other queue functions (especially `pop()`) are called from
     *     the worker's thread, which would have been terminated at this stage.
     *     In other words, the `mutex_` is unlocked when the queue destructor
     *     is called, so we don't have to worry about it being locked while
     *     being destructed.
     */
    ~queue_t() = default;

    /** Constructor
     *
     * \param threshold [in] Queue threshold; must be >0
     */
    explicit queue_t(size_t threshold) : threshold_(threshold)
    {
        skal_assert(threshold_ > 0);
    }

    /** Push a message into the queue
     *
     * This function always succeeds.
     *
     * \param msg [in] Message to push; must not be an empty pointer
     */
    void push(std::unique_ptr<msg_t> msg);

    /** Pop a message from the queue
     *
     * \note This function is blocking. This is the only true blocking function
     *       in the whole skal framework.
     *
     * If the `internal_only` argument is set, urgent and regular messages are
     * ignored, and only internal messages are popped. If there are no internal
     * messages, this function returns an empty pointer regardless of whether or
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
    std::unique_ptr<msg_t> pop(bool internal_only = false);

    size_t size() const
    {
        lock_t lock(mutex_);
        return size_();
    }

    bool is_full() const
    {
        lock_t lock(mutex_);
        return size_() >= threshold_;
    }

    bool is_half_full() const
    {
        lock_t lock(mutex_);
        return size_() > (threshold_ / 2);
    }
};

} // namespace skal
