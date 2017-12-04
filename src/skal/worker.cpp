/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/net.hpp>
#include <skal/log.hpp>
#include <skal/global.hpp>
#include <skal/util.hpp>
#include <skal/semaphore.hpp>
#include <unordered_map>
#include <list>
#include <algorithm>
#include <utility>
#include <regex>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;

/** Global workers mutex
 *
 * Protects all global variables in this module.
 */
std::mutex g_mutex;

/** Type of the worker register
 *
 * This is a hash table indexed by worker names.
 */
typedef std::unordered_map<std::string, std::unique_ptr<worker_t>> workers_t;

/** The worker register */
workers_t g_workers;

/** Names of terminated workers
 *
 * This is used to remove the terminated workers from the worker register.
 */
std::list<std::string> g_terminated_workers;

/** Semaphore to signal `worker_t::wait()` for terminated workers */
ft::semaphore_t g_semaphore;

/** Module possible states */
enum state_t {
    initialising,
    running,
    terminating
};

/** Module current state */
state_t g_state = state_t::initialising;

} // unnamed namespace

class group_t
{
    /** All the subscriptions of a given subscriber
     *
     * The key is the filter string, the value the regex to apply.
     */
    typedef std::map<std::string, std::regex> subscriptions_t;

    /** Subscribers to a given group */
    typedef std::unordered_map<std::string, subscriptions_t> subscribers_t;

    /** All subscribers for this group */
    subscribers_t subscribers_;

    std::string name_;

    void subscribe(const std::string& subscriber_name,
            const std::string& filter);

    /** Remove a subscription
     *
     * \param subscriber_name [in] Name of subscriber to unsubscribe
     * \param filter          [in] Filter string to unsubscribe; can be the
     *                             empty string to remove all subscriptions
     */
    void unsubscribe(const std::string& subscriber_name,
            const std::string& filter);

public :
    group_t(std::string group_name) : name_(full_name(std::move(group_name)))
    {
    }

    bool process(std::unique_ptr<msg_t> msg);
};

void group_t::subscribe(const std::string& subscriber_name,
    const std::string& filter)
{
    std::regex re;
    if (!filter.empty()) {
        try {
            re = std::regex(filter,
                    std::regex::ECMAScript | std::regex::optimize);
        } catch (std::regex_error& e) {
            skal_log(warning) << "Group '" << name_
                << "' received subscription request with invalid regex '"
                << filter << "' from subscriber '" << subscriber_name
                << "': " << e.what() << " - ignored";
            // TODO: raise an alarm
            return;
        }
    }
    auto& subscriber = subscribers_[subscriber_name];
    if (subscriber.find(filter) != subscriber.end()) {
        return; // subscriber already has a subscription for this filter
    }
    skal_log(info) << "Group '" << name_
        << "': adding subscription subscriber='" << subscriber_name
        << "', filter='" << filter << "'";
    subscriber[filter] = std::move(re);
}

void group_t::unsubscribe(const std::string& subscriber_name,
        const std::string& filter)
{
    auto it = subscribers_.find(subscriber_name);
    if (it == subscribers_.end()) {
        return;
    }
    skal_log(info) << "Group '" << name_
        << "': removing subscription subscriber='" << subscriber_name
        << "', filter='" << filter << "'";
    if (filter.empty()) {
        subscribers_.erase(it);
    } else {
        auto it2 = it->second.find(filter);
        if (it2 != it->second.end()) {
            it->second.erase(it2);
        }
    }
}

bool group_t::process(std::unique_ptr<msg_t> msg)
{
    bool ok = true;
    if (start_with(msg->action(), "skal")) {
        if (msg->action() == "skal-init") {
            send_to_skald(msg_t::create_internal("skald", "skal-subscribe"));

        } else if (msg->action() == "skal-exit") {
            if (!subscribers_.empty()) {
                skal_log(info) << "Terminating group '" << name_
                    << "', unsubscribing all current subscribers";
                subscribers_.clear();
            }
            send_to_skald(msg_t::create_internal("skald", "skal-unsubscribe"));

        } else if (msg->action() == "skal-subscribe") {
            std::string filter;
            if (msg->has_string("filter")) {
                filter = msg->get_string("filter");
            }
            subscribe(msg->sender(), filter);

        } else if (msg->action() == "skal-unsubscribe") {
            std::string filter;
            if (msg->has_string("filter")) {
                filter = msg->get_string("filter");
            }
            unsubscribe(msg->sender(), filter);
            if (subscribers_.empty()) {
                ok = false;
            }
        }
    } else {
        for (auto& subscriber : subscribers_) {
            for (auto& subscription : subscriber.second) {
                if (    subscription.first.empty()
                     || std::regex_match(msg->action(), subscription.second)) {
                    auto copy = std::make_unique<msg_t>(msg);
                    copy->recipient(subscriber.first);
                    skal_log(debug) << "Group '" << name_
                        << "': forwarding message from '" << copy->sender()
                        << "' to '" << copy->recipient() << "', action='"
                        << copy->action() << "'";
                    send(std::move(copy));
                }
            } // for each subscription
        } // for each subscriber
    }
    return ok;
}

void send(std::unique_ptr<msg_t> msg)
{
    // Try sending the message internally first, otherwise send to skald
    if (!worker_t::post(msg)) {
        send_to_skald(std::move(msg));
    }
}

void drop(std::unique_ptr<msg_t> msg)
{
    skal_log(debug) << "Dropping message: from='" << msg->sender() << "', to='"
        << msg->recipient() << "', action='" << msg->action() << "'";
}

worker_t::worker_t(params_t params)
    : name_(params.name)
    , params_(params)
    , queue_(params_.queue_threshold)
    , thread_( [this] () { this->thread_entry_point(); } )
{
}

worker_t::~worker_t()
{
    thread_.join();
}

void worker_t::thread_entry_point()
{
    // TODO: NUMA
    global_t::set_me(name_);
    {
        lock_t lock(g_mutex);
        if (g_state == state_t::initialising) {
            // Skal is still initialising => Wait for the green light
            lock.unlock();
            semaphore_.take();
        }
    }
    try {
        run();
    } catch (std::exception& e) {
        skal_log(error) << "Thread of worker '" << name_
            << "' unexpectedly threw exception: " << e.what();
        // TODO: raise alarm
    } catch (...) {
        skal_log(error) << "Thread of worker '" << name_
            << "' unexpectedly threw a non-standard exception";
        // TODO: raise alarm
    }
    lock_t lock(g_mutex);
    g_terminated_workers.push_back(name_);
    g_semaphore.post();
}

void worker_t::run()
{
    // NB: The worker object will not be accessed in any way outside this
    //     thread, except for when messages are pushed into its queue, which is
    //     an MT-safe operation. In other words, the code here does not need to
    //     worry about thread safety.

    skal_log(info) << "Starting worker '" << name_ << "'";
    send(msg_t::create_internal("skald", "skal-born"));
    queue_.push(msg_t::create("", name_, "skal-init"));

    bool stop = false;
    bool throttled = false;
    while (!stop) {
        bool internal_only = false; // Flag: process internal only or all msg?
        if (!xoff_.empty()) { // Other workers blocked me
            if (!throttled) {
                queue_.push(msg_t::create_internal("",
                            name_, "skal-throttle-on"));
                throttled = true;
            }
            auto now = std::chrono::steady_clock::now();
            if ((last_xoff_ + params_.xoff_timeout) < now) {
                skal_log(debug) << "Worker '" << name_
                    << "' resumes after xoff_timeout";
                xoff_.clear();
            } else {
                internal_only = true;
            }
        } else {
            if (throttled) {
                queue_.push(msg_t::create_internal("",
                            name_, "skal-throttle-off"));
                throttled = false;
            }
        }
        std::unique_ptr<msg_t> msg = queue_.pop(internal_only);
        skal_assert(msg);
        timepoint_t start = std::chrono::steady_clock::now();
        if (msg->iflags() & msg_t::iflag_t::internal) {
            if (!process_internal_msg(msg)) {
                stop = true;
            }
        }
        if (params_.process_msg) {
            skal_log(debug) << "Worker '" << name_ << "': processing message '"
                << msg->action() << "' from '" << msg->sender() << "'";
            try {
                if (!params_.process_msg(std::move(msg))) {
                    skal_log(info) << "Worker '" << name_ << "' stopped";
                    stop = true;
                }
            } catch (std::exception& e) {
                skal_log(notice) << "Worker '" << name_
                    << "' threw an exception: " << e.what();
                stop = true;
                // TODO: raise an alarm
            } catch (...) {
                skal_log(notice) << "Worker '" << name_
                    << "' threw a non-standard exception";
                stop = true;
                // TODO: raise an alarm
            }
        }
        timepoint_t end = std::chrono::steady_clock::now();
        std::chrono::nanoseconds duration = end - start;
        (void)duration; // TODO: fill in report

        if (stop || !queue_.is_half_full()) {
            // My queue is not full anymore or I am terminated
            //  => Release peer workers I am currently blocking
            send_xon();
        }
    } // infinite loop

    // Unsubscribe from all groups this worker subscribed to. Careful: sending
    // a "skal-unsubscribe" message will actually modify the `subscriptions_`.
    std::vector<std::unique_ptr<msg_t>> messages;
    for (auto& subscription : subscriptions_) {
        for (auto& filter : subscription.second) {
            messages.push_back(msg_t::create(subscription.first, filter));
        }
    }
    for (auto& msg : messages) {
        send(std::move(msg));
    }

    if (params_.process_msg) {
        params_.process_msg(msg_t::create("", name_, "skal-exit"));
    }
    send(msg_t::create_internal("skald", "skal-died"));
    skal_log(info) << "Worker '" << name_ << "' terminated";
}

bool worker_t::process_internal_msg(const std::unique_ptr<msg_t>& msg)
{
    skal_log(debug) << "Worker '" << name_ << "': processing internal message '"
        << msg->action() << "' from '" << msg->sender() << "'";
    bool ok = true;
    if (msg->action() == "skal-xoff") {
        // A worker is telling me to stop sending to it
        last_xoff_ = std::chrono::steady_clock::now();
        xoff_.insert(msg->sender());

    } else if (msg->action() == "skal-xon") {
        // A worker is telling me I can resume sending
        xoff_.erase(msg->sender());

    } else if (msg->action() == "skal-terminate") {
        ok = false;
    }
    return ok;
}

void worker_t::send_xon()
{
    for (auto& worker_name : ntf_xon_) {
        skal_log(debug) << "Worker '" << name_ << "': peer worker '"
            << worker_name << "' is blocked by me; sending it 'skal-xon'";
        send(msg_t::create_internal(worker_name, "skal-xon"));
    }
    ntf_xon_.clear();
}

void worker_t::create(params_t params)
{
    lock_t lock(g_mutex);
    params.name = full_name(params.name);
    if (g_state == state_t::terminating) {
        throw terminating_error();
    }
    if (g_workers.find(params.name) != g_workers.end()) {
        throw duplicate_error();
    }
    // NB: Can't use `make_unique` here because constructor is private
    g_workers.emplace(params.name,
            std::unique_ptr<worker_t>(new worker_t(params)));
    skal_log(debug) << "Added worker '" << params.name << "' to the register";
}

bool worker_t::post(std::unique_ptr<msg_t>& msg)
{
    if (start_with(msg->recipient(), "skald")) {
        return false; // "skald.*" is definitely not in this process
    }
    lock_t lock(g_mutex);
    workers_t::iterator it = g_workers.find(msg->recipient());
    if (it == g_workers.end()) {
        if (msg->action() == "skal-subscribe") {
            // The sender wants to subscribe to a group that doesn't exist yet
            auto group = std::make_shared<group_t>(msg->recipient());
            // TODO: NUMA
            // Create a worker with the same name as the group
            // NB: The closure object will own the `group_t` object, which
            //     will be destructed when the worker object will be destructed.
            worker_t::create(msg->recipient(),
                    [group] (std::unique_ptr<msg_t> msg) mutable
                    {
                        return group->process(std::move(msg));
                    });
            it = g_workers.find(msg->recipient());
            skal_assert(it != g_workers.end());
        } else {
            skal_log(debug) << "Can't post message to worker '"
                << msg->recipient() << "': no such worker in this process";
            return false;
        }
    }
    skal_assert(it->second);

    if (    (msg->action() == "skal-subscribe")
         || (msg->action() == "skal-unsubscribe")) {
        // Update the destination worker's internal structures
        // NB: They are used to keep track of this worker's subscriptions, and
        //     to unsubscribe the worker from all its current subscriptions
        //     when it is terminated.
        auto it2 = g_workers.find(msg->sender());
        if (it2 != g_workers.end()) {
            std::string filter;
            if (msg->has_string("filter")) {
                filter = msg->get_string("filter");
            }
            if (msg->action() == "skal-subscribe") {
                it2->second->subscriptions_[msg->sender()].insert(filter);
            } else {
                it2->second->subscriptions_[msg->sender()].erase(filter);
            }
        }
    }

    bool tell_xoff = false;
    std::string sender;
    if (it->second->queue_.is_full()) {
        // The recipient queue is full => Throttle the sender
        if (    !(msg->iflags() & msg_t::iflag_t::internal)
             && !msg->sender().empty()) {
            // NB: Both internal and external messages are excluded from the
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

    // Deliver the message to its recipient, even when its queue is full
    it->second->queue_.push(std::move(msg));

    if (tell_xoff) {
        // Send a "skal-xoff" message to `sender`
        // NB: We moved the `msg` variable, so don't access it any more
        msg = msg_t::create_internal(it->second->name_, sender, "skal-xoff");
        it->second->ntf_xon_.insert(sender);

        // NB: Better be on the safe side and avoid recursing into
        //     `worker_t::post()`
        auto it2 = g_workers.find(sender);
        if (it2 != g_workers.end()) {
            skal_assert(it2->second);
            it2->second->queue_.push(std::move(msg));
        } else {
            send_to_skald(std::move(msg));
        }
    }
    return true;
}

void worker_t::wait()
{
    global_t::set_me("main"); // Set thread name for nice logging
    skal_log(info) << "Running skal application";
    {
        lock_t lock(g_mutex);
        g_state = state_t::running;
    }
    for (auto& worker : g_workers) {
        worker.second->semaphore_.post();
    }
    for (;;) {
        g_semaphore.take();
        lock_t lock(g_mutex);
        while (!g_terminated_workers.empty()) {
            g_workers.erase(g_terminated_workers.front());
            g_terminated_workers.pop_front();
        }
        if (g_workers.empty()) {
            skal_log(debug) << "No more workers, skal application terminated";
            break;
        }
    }
    // Reset the state, if the client software calls `wait()` again
    g_state = state_t::initialising;
}

void worker_t::terminate()
{
    lock_t lock(g_mutex);
    g_state = state_t::terminating;
    for (workers_t::iterator it = g_workers.begin();
            it != g_workers.end(); ++it) {
        it->second->queue_.push(msg_t::create_internal(it->first,
                    "skal-terminate"));
    }
}

} // namespace skal
