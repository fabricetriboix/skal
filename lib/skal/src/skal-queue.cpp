/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "detail/skal-queue.hpp"
#include "detail/skal-log.hpp"

namespace skal {

void queue_t::push(msg_t msg)
{
    lock_t lock(mutex_);

    if (msg.iflags() & flag_t::internal) {
        internal_.push_back(std::move(msg));
    } else if (msg.flags() & flag_t::urgent) {
        urgent_.push_back(std::move(msg));
    } else {
        regular_.push_back(std::move(msg));
    }

    if (ntf_) {
        ntf_();
    }
    cv_.notify_one();
}

msg_t queue_t::pop_BLOCKING(bool internal_only)
{
    lock_t lock(mutex_);

    // Wait for a message being available
    if (internal_only) {
        while (internal_.empty()) {
            cv_.wait(lock);
        }
    } else {
        while (internal_.empty() && urgent_.empty() && regular_.empty()) {
            cv_.wait(lock);
        }
    }

    if (!internal_.empty()) {
        msg_t msg = std::move(internal_.front());
        internal_.pop_front();
        return std::move(msg);

    } else if (!urgent_.empty()) {
        msg_t msg = std::move(urgent_.front());
        urgent_.pop_front();
        return std::move(msg);

    } else {
        skal_assert(!regular_.empty());
        msg_t msg = std::move(regular_.front());
        regular_.pop_front();
        return std::move(msg);
    }
}

msg_t queue_t::pop(bool internal_only)
{
    lock_t lock(mutex_);

    if (!internal_.empty()) {
        msg_t msg = std::move(internal_.front());
        internal_.pop_front();
        return std::move(msg);

    } else if (!internal_only) {
        if (!urgent_.empty()) {
            msg_t msg = std::move(urgent_.front());
            urgent_.pop_front();
            return std::move(msg);

        } else if (!regular_.empty()) {
            msg_t msg = std::move(regular_.front());
            regular_.pop_front();
            return std::move(msg);
        }
    }

    throw std::out_of_range("empty queue");
}

size_t queue_t::size() const
{
    lock_t lock(mutex_);
    return internal_.size() + urgent_.size() + regular_.size();
}

bool queue_t::is_full() const
{
    lock_t lock(mutex_);
    size_t size = internal_.size() + urgent_.size() + regular_.size();
    return size >= threshold_;
}

bool queue_t::is_half_full() const
{
    lock_t lock(mutex_);
    size_t size = internal_.size() + urgent_.size() + regular_.size();
    return size >= (threshold_ / 2);
}

} // namespace skal
