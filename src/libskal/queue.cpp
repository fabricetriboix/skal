/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "internal/queue.hpp"
#include "internal/msg.hpp"
#include <skal/detail/log.hpp>

namespace skal {

void queue_t::push(std::unique_ptr<msg_t> msg)
{
    skal_assert(msg);
    lock_t lock(mutex_);
    if (msg->iflags() & iflag_t::internal) {
        internal_.push_back(std::move(msg));
    } else if (msg->flags() & flag_t::urgent) {
        urgent_.push_back(std::move(msg));
    } else {
        regular_.push_back(std::move(msg));
    }
    if (ntf_) {
        ntf_();
    }
}

std::unique_ptr<msg_t> queue_t::pop(bool internal_only)
{
    lock_t lock(mutex_);
    std::unique_ptr<msg_t> msg;
    if (!internal_.empty()) {
        msg = std::move(internal_.front());
        internal_.pop_front();
    } else if (!internal_only) {
        if (!urgent_.empty()) {
            msg = std::move(urgent_.front());
            urgent_.pop_front();
        } else if (!regular_.empty()) {
            msg = std::move(regular_.front());
            regular_.pop_front();
        }
    }
    return std::move(msg);
}

size_t queue_t::size() const
{
    lock_t lock(mutex_);
    return calc_size();
}

bool queue_t::is_full() const
{
    lock_t lock(mutex_);
    size_t size = calc_size();
    return size >= threshold_;
}

bool queue_t::is_half_full() const
{
    lock_t lock(mutex_);
    size_t size = calc_size();
    return size >= (threshold_ / 2);
}

} // namespace skal
