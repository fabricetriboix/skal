/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/net.hpp>
#include <skal/log.hpp>
#include <skal/global.hpp>
#include <skal/util.hpp>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <regex>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;
typedef std::unordered_map<std::string, std::unique_ptr<worker_t>> workers_t;
workers_t g_workers;
std::mutex g_mutex;

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
        skal_log(info) << "XXX group_t " << name_ << " constructor";
    }
    ~group_t() {
        skal_log(info) << "XXX group_t " << name_ << "destructor";
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
    skal_log(debug) << "Group '" << name_
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
    skal_log(debug) << "Group '" << name_
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
                skal_log(debug) << "Terminating group '" << name_
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

worker_t::worker_t(std::string name, process_msg_t process_msg, int numa_node,
        int64_t queue_threshold, std::chrono::nanoseconds xoff_timeout)
    : name_(full_name(name))
    , process_msg_(process_msg)
    , queue_(queue_threshold)
    , xoff_timeout_(xoff_timeout)
    , thread_( [this] () { this->run_safe(); } )
{
    // TODO: NUMA
    skal_log(debug) << "Creating worker '" << name_ << "'";
}

worker_t::~worker_t()
{
    skal_log(debug) << "Destroying worker '" << name_ << "'";
    thread_.join();
}

void worker_t::run_safe()
{
    try {
        run();
    } catch (std::exception& e) {
        skal_log(error) << "Thread entry point for worker '" << name_
            << "' threw exception: " << e.what();
        // TODO: raise alarm
    } catch (...) {
        skal_log(error) << "Thread entry point for worker '" << name_
            << "' threw a non-standard exception";
        // TODO: raise alarm
    }
}

void worker_t::run()
{
    // NB: This is the thread entry point for the worker. The worker object
    //     will not be accessed in any way outside this thread, except for when
    //     messages are pushed into its queue, which is an MT-safe operation.
    //     In other words, the code here does not need to worry about thread
    //     safety.

    global_t::set_me(name_);
    send(msg_t::create_internal("skald", "skal-born"));

    bool stop = false;
    while (!stop) {
        bool internal_only = false; // Flag: process internal only or all msg?
        if (!xoff_.empty()) { // Other workers blocked me
            if (last_xoff_ > std::chrono::steady_clock::now()) {
                skal_log(debug) << "Worker '" << name_
                    << "' resumes after xoff_timeout";
                xoff_.clear();
            } else {
                internal_only = true;
            }
        }
        std::unique_ptr<msg_t> msg = queue_.pop(internal_only);
        skal_assert(msg);
        timepoint_t start = std::chrono::steady_clock::now();
        if (msg->iflags() & msg_t::iflag_t::internal) {
            if (!process_internal_msg(std::move(msg))) {
                stop = true;
            }
        } else if (process_msg_) {
            skal_log(debug) << "Worker '" << name_ << "': processing message '"
                << msg->action() << "' from '" << msg->sender() << "'";
            try {
                if (!process_msg_(std::move(msg))) {
                    skal_log(info) << "Worker '" << name_
                        << "' terminated naturally";
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

    if (process_msg_) {
        process_msg_(msg_t::create("", name_, "skal-exit"));
    }
    send(msg_t::create_internal("skald", "skal-died"));
}

bool worker_t::process_internal_msg(std::unique_ptr<msg_t> msg)
{
    bool ok = true;
    if (msg->action() == "skal-xoff") {
        // A worker is telling me to stop sending to it
        xoff_.insert(msg->sender());
        last_xoff_ = std::chrono::steady_clock::now();

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

void worker_t::create(std::string name, process_msg_t process_msg,
        int numa_node, int64_t queue_threshold,
        std::chrono::nanoseconds xoff_timeout)
{
    lock_t lock(g_mutex);
    name = full_name(std::move(name));
    if (g_workers.find(name) != g_workers.end()) {
        throw duplicate_error();
    }

    // NB: Can't use `make_unique` here because constructor is private
    g_workers.emplace(name, std::unique_ptr<worker_t>(new worker_t(name,
                    process_msg, numa_node, queue_threshold, xoff_timeout)));
    skal_log(debug) << "Added worker '" << name << "' to register";
    g_workers[name]->queue_.push(msg_t::create("", name, "skal-init"));
}

bool worker_t::post(std::unique_ptr<msg_t>& msg)
{
    if (start_with(msg->recipient(), "skald")) {
        return false; // skald is definitely not in this process
    }
    lock_t lock(g_mutex);
    workers_t::iterator it = g_workers.find(msg->recipient());
    if (it == g_workers.end()) {
        if (msg->action() != "skal-subscribe") {
            skal_log(debug) << "Can't post message to worker '"
                << msg->recipient() << "': no such worker in this process";
            return false;
        }

        // The sender wants to subscribe to a group that doesn't exist yet
        auto group = std::make_shared<group_t>(msg->recipient());
        // TODO: NUMA
        worker_t::create(msg->recipient(),
                [group] (std::unique_ptr<msg_t> msg) mutable
                {
                    return group->process(std::move(msg));
                });
        it = g_workers.find(msg->recipient());
        skal_assert(it != g_workers.end());
    }
    skal_assert(it->second);

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
    it->second->queue_.push(std::move(msg));
    if (tell_xoff) {
        // Send a "skal-xoff" message to `sender`
        // NB: We moved `msg`, so don't access it any more
        msg = msg_t::create_internal(it->second->name_, sender, "skal-xoff");
        it->second->ntf_xon_.insert(sender);

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

} // namespace skal
