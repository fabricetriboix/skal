/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/detail/log.hpp>
#include <skal/detail/cfg.hpp>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <sstream>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;
typedef std::unordered_map<std::string, worker_t*> workers_t;
workers_t g_workers;
std::mutex g_mutex;

} // unnamed namespace

std::unique_ptr<worker_t> worker_t::create(std::string name, process_t process,
        int64_t queue_threshold, std::chrono::nanoseconds xoff_timeout)
{
    lock_t lock(g_mutex);
    if (g_workers.find(name) != g_workers.end()) {
        throw duplicate_error();
    }
    // NB: Can't use `make_unique` here because constructor is private
    std::unique_ptr<worker_t> worker = std::unique_ptr<worker_t>
        (new worker_t(name, process, queue_threshold, xoff_timeout));
    g_workers[worker->name_] = worker.get();
    skal_log(debug) << "Created and registered worker '"
        << worker->name() << "'";
    return std::move(worker);
}

worker_t::~worker_t()
{
    skal_log(debug) << "Removing worker '" << name_ << "' from the register";
    lock_t lock(g_mutex);
    g_workers.erase(name_);
}

bool worker_t::post(std::unique_ptr<msg_t>& msg)
{
    lock_t lock(g_mutex);
    workers_t::iterator it = g_workers.find(msg->recipient());
    if (it == g_workers.end()) {
        return false;
    }

    bool tell_xoff = false;
    std::string sender;
    if (it->second->queue_.is_full()) {
        // The recipient queue is full
        if (msg->flags() & msg_t::flag_t::drop_ok) {
            // Message can be dropped, so drop it now
            drop(std::move(msg));
            return true;
        }
        if (!(msg->iflags() & msg_t::iflag_t::internal)) {
            // NB: Internal messages are excluded from the throttling mechanism
            tell_xoff = true;
        }
        if (tell_xoff) {
            sender = msg->sender();
        }
    }

    // NB: We moved the `msg`, so don't access it any more
    it->second->queue_.push(std::move(msg));

    if (tell_xoff) {
        std::unique_ptr<msg_t> xoff_msg = msg_t::create_internal(
                it->second->name_, std::move(sender), "skal-xoff");
        if (!post(xoff_msg)) {
            // TODO: send to skald
        }
    }
    return true;
}

void worker_t::drop(std::unique_ptr<msg_t> msg)
{
    std::unique_ptr<msg_t> drop_msg = msg_t::create_internal(
            worker_name("skal-internal"), msg->sender(), "skal-error-drop");
    msg->add_field("reason", "no recipient");
    std::ostringstream oss;
    oss << "Worker '" << msg->recipient() << "' does not exist";
    msg->add_field("extra", oss.str());
    if (!post(drop_msg)) {
        // TODO: send to skald
    }
}

} // namespace skal
