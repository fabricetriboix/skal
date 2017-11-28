/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/net.hpp>
#include <skal/log.hpp>
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
    skal_log(info) << "Worker '" << worker->name() << "' created";

    worker->queue_.push(msg_t::create(worker->name(), "skal-init"));
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
        skal_log(debug) << "Can't post message to worker '" << msg->recipient()
            << "': no such worker in this process";
        return false;
    }
    skal_assert(it->second);

    bool tell_xoff = false;
    std::string sender;
    if (it->second->queue_.is_full()) {
        // The recipient queue is full
        if (    !(msg->iflags() & msg_t::iflag_t::internal)
             && !msg->sender().empty()) {
            // NB: Internal and external messages are excluded from the
            //     throttling mechanism
            tell_xoff = true;
        }
        if (tell_xoff) {
            sender = msg->sender();
            skal_log(debug) << "Sender '" << sender
                << "' is sending messages too fast to '" << msg->recipient()
                << "'; sending it a 'skal-xoff' message";
        }
    }

    it->second->queue_.push(std::move(msg));

    if (tell_xoff) {
        // NB: We moved the `msg`, so don't access it any more
        std::unique_ptr<msg_t> xoff_msg = msg_t::create_internal(
                it->second->name_, sender, "skal-xoff");
        it->second->ntf_xon_.insert(xoff_msg->recipient());

        auto it2 = g_workers.find(sender);
        if (it2 != g_workers.end()) {
            skal_assert(it2->second);
            it2->second->queue_.push(std::move(xoff_msg));
        } else {
            send_to_skald(std::move(xoff_msg));
        }
    }
    return true;
}

void worker_t::drop(std::unique_ptr<msg_t> msg)
{
    skal_log(debug) << "Dropping message: from='" << msg->sender() << "', to='"
        << msg->recipient() << "', action='" << msg->action() << "'";
}

bool worker_t::process_one()
{
    bool internal_only = !xoff_.empty();
    std::unique_ptr<msg_t> msg = queue_.pop(internal_only);
    if (!msg) {
        std::ostringstream oss;
        oss << "Worker '" << name_
            << "' told to process one message, but its queue is empty; "
            << "this is a bug, please fix it";
        throw std::underflow_error(oss.str());
    }

    time_point_t start = std::chrono::steady_clock::now();
    bool stop = false;
    bool is_internal = msg->iflags() & msg_t::iflag_t::internal;
    if (is_internal) {
        skal_log(debug) << "Worker '" << name_
            << "': processing internal message '" << msg->action()
            << "' from '" << msg->sender() << "'";
        stop = process_internal_msg(std::move(msg));
    }
    if (!ntf_xon_.empty() && !queue_.is_half_full()) {
        // Some workers are waiting for my queue not to be full anymore
        send_xon();
    }

    if (!is_internal) {
        // Message not processed yet
        skal_log(debug) << "Worker '" << name_ << "': processing message '"
            << msg->action() << "' from '" << msg->sender() << "'";
        try {
            if (!process_(std::move(msg))) {
                skal_log(info) << "Worker '" << name_
                    << "' terminated naturally";
                stop = true;
            }
        } catch (std::exception& e) {
            std::ostringstream oss;
            oss << "Worker '" << name_ << "' threw an exception: " << e.what();
            skal_log(notice) << oss.str();
            stop = true;
            // TODO: raise an alarm
        } catch (...) {
            std::ostringstream oss;
            oss << "Worker '" << name_ << "' threw an exception";
            skal_log(notice) << oss.str();
            stop = true;
            // TODO: raise an alarm
        }
    }
    time_point_t end = std::chrono::steady_clock::now();
    auto duration = end - start;
    (void)duration; // TODO: do something with that

    if (!xoff_.empty()) {
        // I am blocked => Send reminders to workers who are blocking me
        send_ntf_xon(end);
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
        skal_log(debug) << "Worker '" << name_ << "': peer worker '"
            << worker_name << "' is blocked by me; sending it 'skal-xon'";
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
            skal_log(debug) << "Worker '" << name_ << "' has been blocked by '"
                << xoff.first
                << "' for a while now, sending it 'skal-ntf-xon'";
            send(msg_t::create_internal(name_, xoff.first, "skal-ntf-xon"));
            xoff.second = now;
        }
    }
}

void send(std::unique_ptr<msg_t> msg)
{
    // Try sending the message internally first, otherwise send to skald
    if (!worker_t::post(msg)) {
        send_to_skald(std::move(msg));
    }
}

} // namespace skal
