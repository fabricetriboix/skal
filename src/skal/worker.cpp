/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/log.hpp>
#include <skal/cfg.hpp>
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

void worker_t::send(std::unique_ptr<msg_t> msg)
{
    if (!post(msg)) {
        // TODO: forward to skald
    }
}

bool worker_t::post(std::unique_ptr<msg_t>& msg)
{
    lock_t lock(g_mutex);
    workers_t::iterator it = g_workers.find(msg->recipient());
    if (it == g_workers.end()) {
        return false;
    }
    skal_assert(it->second);

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
        it->second->ntf_xon_.insert(xoff_msg->recipient());
        send(std::move(xoff_msg));
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
    send(std::move(drop_msg));
}

bool worker_t::process_one()
{
    bool internal_only = !xoff_.empty();
    std::unique_ptr<msg_t> msg = queue_.pop(internal_only);
    skal_assert(msg) << "Worker '" << name_
        << "' told to process one message, but its queue is empty";

    bool stop = false;
    if (msg->iflags() & msg_t::iflag_t::internal) {
        stop = process_internal_msg(std::move(msg));
    }
    if (!ntf_xon_.empty() && !queue_.is_half_full() && !stop) {
        // Some workers are waiting for my queue not to be full anymore
        send_xon();
    }

    if (!(msg->iflags() & msg_t::iflag_t::internal)) {
        time_point_t start = std::chrono::steady_clock::now();
        try {
            process_(std::move(msg));
        } catch (std::exception& e) {
            std::ostringstream oss;
            oss << "Worker '" << name_ << "' threw an exception: " << e.what();
            skal_log(error) << oss.str();
            stop = true;
            // TODO: raise an alarm
        } catch (...) {
            std::ostringstream oss;
            oss << "Worker '" << name_ << "' threw an exception";
            skal_log(error) << oss.str();
            stop = true;
            // TODO: raise an alarm
        }
        time_point_t end = std::chrono::steady_clock::now();
        auto duration = end - start;
        (void)duration; // TODO: do something with that
    }

    if (!xoff_.empty()) {
        // I am blocked => Send reminders to workers who are blocking me
        send_ntf_xon(std::chrono::steady_clock::now());
    }

    if (stop) {
        // This worker is now terminated; release any worker blocked on me
        send_xon();
    }
    return stop;
}

bool worker_t::process_internal_msg(std::unique_ptr<msg_t> msg)
{
    bool terminate = false;
    if (msg->action() == "skal-xoff") {
        // A worker is telling me to stop sending to it
        xoff_[msg->sender()] = std::chrono::steady_clock::now();

    } else if (msg->action() == "skal-xon") {
        // A worker is telling me I can resume sending
        xoff_.erase(msg->sender());

    } else if (msg->action() == "skal-ntf-xon") {
        // A worker I am blocking is telling me to notify it when it can send
        // messages again
        ntf_xon_.insert(msg->sender());

    } else if (msg->action() == "skal-terminate") {
        terminate = true;
    }
    return terminate;
}

void worker_t::send_xon()
{
    for (auto& worker_name : ntf_xon_) {
        send(msg_t::create_internal(name_, worker_name, "skal-xon"));
    }
    ntf_xon_.clear();
}

void worker_t::send_ntf_xon(std::chrono::steady_clock::time_point now)
{
    for (auto& xoff : xoff_) {
        auto elapsed = now - xoff.second;
        if (elapsed > xoff_timeout_) {
            // We waited for quite a while for an "skal-xon" message
            //  => Poke the worker that is blocking me
            send(msg_t::create_internal(name_, xoff.first, "skal-ntf-xon"));
            xoff.second = now;
        }
    }
}

} // namespace skal
