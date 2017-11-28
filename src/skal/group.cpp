/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/group.hpp>
#include <skal/log.hpp>
#include <skal/global.hpp>
#include <skal/executor.hpp>
#include <skal/net.hpp>
#include <skal/util.hpp>
#include <unordered_map>
#include <algorithm>
#include <utility>
#include <sstream>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;
typedef std::unordered_map<std::string, std::unique_ptr<group_t>> groups_t;
groups_t g_groups;
std::mutex g_mutex;

} // unnamed namespace

group_t::group_t(std::string group_name, executor_t* executor)
    : name_(full_name(std::move(group_name)))
    , is_explicit(executor != nullptr)
{
    skal_log(debug) << (is_explicit ? "Explicitely" : "Automatically")
        << " creating group '" << name_ << "'";

    if (executor == nullptr) {
        executor = executor_t::get_arbitrary_executor();
    }
    skal_assert(executor != nullptr);
    executor->add_worker(worker_t::create(name_,
                [this] (std::unique_ptr<msg_t> msg)
                {
                    this->forward(std::move(msg));
                    return true;
                }));

    auto msg = msg_t::create_internal("", "skald", "skal-create-group");
    msg->add_field("name", name_);
    send_to_skald(std::move(msg));
}

group_t::~group_t()
{
    skal_log(debug) << (is_explicit ? "Explicitely" : "Automatically")
        << " destroying group '" << name_ << "'";
    safe_lock_t lock(mutex_);
    subscribers_.clear();

    // Terminate the group's worker
    auto msg = msg_t::create_internal("", name_, "skal-terminate");
    bool posted = worker_t::post(msg);
    skal_assert(posted);

    msg = msg_t::create_internal("", "skald", "skal-destroy-group");
    msg->add_field("name", name_);
    send_to_skald(std::move(msg));
}

void group_t::forward(std::unique_ptr<msg_t> msg)
{
    safe_lock_t lock(mutex_);
    if (start_with(msg->action(), "skal")) {
        // Do not forward messages from the skal framework to the worker
        // itself, such as "skal-init".
        return;
    }
    for (auto& subscriber : subscribers_) {
        for (auto& subscription : subscriber.second) {
            if (    subscription.first.empty()
                 || std::regex_match(msg->action(), subscription.second)) {
                auto copy = std::make_unique<msg_t>(msg);
                copy->recipient(subscriber.first);
                skal_log(debug) << "Group '" << name_
                    << "': forwarding message from '" << copy->sender()
                    << "' to '" << copy->recipient();
                send(std::make_unique<msg_t>(msg));
            }
        } // for each subscription
    } // for each subscriber
}

void group_t::create(std::string group_name, executor_t& executor)
{
    lock_t lock(g_mutex);
    group_name = full_name(std::move(group_name));
    if (g_groups.find(group_name) != g_groups.end()) {
        throw duplicate_error();
    }
    g_groups[group_name] = std::unique_ptr<group_t>(
            new group_t(group_name, &executor));
}

void group_t::destroy(std::string group_name)
{
    lock_t lock(g_mutex);
    group_name = full_name(std::move(group_name));
    g_groups.erase(group_name);
}

void group_t::subscribe(std::string group_name,
        std::string subscriber_name, std::string filter)
{
    lock_t lock(g_mutex);
    group_name = full_name(std::move(group_name));
    auto it = g_groups.find(group_name);
    if (it == g_groups.end()) {
        // No such group => create it
        auto result = g_groups.emplace(group_name,
                std::unique_ptr<group_t>(new group_t(group_name)));
        it = result.first;
    }
    skal_assert(it->second);
    safe_lock_t lock2(it->second->mutex_);
    lock.release();

    subscriber_name = full_name(std::move(subscriber_name));
    auto& subscriber = it->second->subscribers_[subscriber_name];
    if (subscriber.find(filter) != subscriber.end()) {
        return; // this subscriber already has a subscription for this filter
    }
    skal_log(debug) << "Adding subscription: group='" << group_name
        << "', subscriber='" << subscriber_name
        << "', filter='" << filter << "'";
    std::regex re;
    if (!filter.empty()) {
        try {
            re = std::regex(filter,
                    std::regex::ECMAScript | std::regex::optimize);
        } catch (std::regex_error& e) {
            skal_log(error) << "Group '" << group_name
                << "' received invalid regex '" << filter
                << "' for subscriber '" << subscriber_name << "' - ignored";
            // TODO: raise an alarm
            return;
        }
    }
    subscriber[filter] = std::move(re);
    lock2.release();

    auto msg = msg_t::create_internal("", "skald", "skal-subscribe");
    msg->add_field("subscriber", subscriber_name);
    msg->add_field("filter", filter);
    send_to_skald(std::move(msg));
}

void group_t::unsubscribe(std::string group_name,
        std::string subscriber_name, std::string filter)
{
    lock_t lock(g_mutex);
    group_name = full_name(std::move(group_name));
    auto it = g_groups.find(group_name);
    if (it == g_groups.end()) {
        return; // No such group => Nothing to do
    }
    skal_assert(it->second);
    safe_lock_t lock2(it->second->mutex_);
    lock.release();

    subscriber_name = full_name(std::move(subscriber_name));
    if (filter.empty()) {
        skal_log(debug) << "Group '" << group_name
            << "': removing all subscriptions for subscriber '"
            << subscriber_name << "'";
        it->second->subscribers_.erase(subscriber_name);
    } else {
        auto it2 = it->second->subscribers_.find(subscriber_name);
        if (it2 != it->second->subscribers_.end()) {
            skal_log(debug) << "Group '" << group_name
                << "': removing subscription: subscriber='" << subscriber_name
                << "', filter='" << filter << "'";
            it2->second.erase(filter);
        }
    }
    lock2.release();

    auto msg = msg_t::create_internal("", "skald", "skal-unsubscribe");
    msg->add_field("subscriber", subscriber_name);
    msg->add_field("filter", filter);
    send_to_skald(std::move(msg));
}

} // namespace skal
