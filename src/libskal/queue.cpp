/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/detail/queue.hpp>
#include <skal/detail/msg.hpp>
#include <skal/detail/log.hpp>

namespace skal {

void queue_t::push(msg_t::ptr_t msg)
{
    skal_assert(msg);
    if (msg->iflags() & iflag_t::internal) {
        internal_.push_back(std::move(msg));
    } else if (msg->flags() & flag_t::urgent) {
        urgent_.push_back(std::move(msg));
    } else {
        regular_.push_back(std::move(msg));
    }
    for (auto& ntf : ntf_) {
        ntf();
    }
}

msg_t::ptr_t queue_t::pop(bool internal_only)
{
    msg_t::ptr_t msg;
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

} // namespace skal
