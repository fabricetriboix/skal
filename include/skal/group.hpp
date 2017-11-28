/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <skal/worker.hpp>
#include <skal/executor.hpp>
#include <skal/safe-mutex.hpp>
#include <unordered_map>
#include <regex>
#include <mutex>
#include <boost/noncopyable.hpp>

namespace skal {

/** Class that represents a multicast group
 *
 * A multicast group has a name similar to a worker's. In actuality, a
 * multicast group instantiates a worker with this name to implement the
 * multicast functionality.
 *
 * This class is MT-safe.
 */
class group_t final : boost::noncopyable
{
public :
    ~group_t();

    /** Explicitely create a multicast group
     *
     * This function is useful if you want to create a group with its worker
     * allocated to a specific executor.
     *
     * \note An explicitely created group stays alive even if it has no
     *       subscribers. Such a group can still be destroyed using the
     *       `destroy()` function.
     *
     * \param group_name [in] Name of the new group
     * \param executor   [in] Executor where to run the group's worker
     *
     * \throw `duplicate_error` if a group or a worker already exists with the
     *        same name `group_name`.
     */
    static void create(std::string group_name, executor_t& executor);

    /** Explicitely destroy a multicast group
     *
     * Any subscriber will be unsubscribed. If there is no group with that
     * `name`, no action is taken.
     *
     * \param group_name [in] Name of group to destroy
     */
    static void destroy(std::string group_name);

    /** Add a subscription to the given group
     *
     * If the specified group does not exist, it is created automatically.
     * Please note that in such a case, its worker will be allocated to an
     * executor in an arbitrary manner. If you want to have the group's
     * worker allocated to a specific executor, please use the `create()`
     * function beforehand.
     *
     * A subscription is a combination of `subscriber_name` + `filter`. This
     * way, a subscriber can have multiple subscriptions with different
     * filters.
     *
     * If the `filter` string is empty, all group messages will be forwarded to
     * the `subscriber_name`.
     *
     * If the `filter` string is not empty, it must be a regular expression
     * which will be matched against the group messages action strings. If a
     * message's action string matches, that message will be forwarded to
     * `subscriber_name` (which can be a worker or a group). The grammar used
     * by the regular expression is the default `std::regex` grammar, which is
     * ECMAScript.
     *
     * If a subscription already exists for that subscriber with the same
     * `filter` string, no action is taken.
     *
     * \param group_name      [in] Name of group to subscribe to
     * \param subscriber_name [in] Worker or group who wants to subscribe
     * \param filter          [in] Filter on message action strings
     *
     * \throw `duplicate_error` if a worker named `group_name` already exists
     *        (this can happen if the group needs to be created and the group
     *        has the same name as an existing worker)
     */
    static void subscribe(std::string group_name,
            std::string subscriber_name, std::string filter = "");

    /** Remove a subscription from the given group
     *
     * If there is no subscriber with that `subscriber_name`, or no
     * subscription with that `filter` string, no action is taken.
     *
     * \param group_name      [in] Name of group to unsubscribe from
     * \param subscriber_name [in] Worker or group who wants to unsubscribe
     * \param filter          [in] Corresponding filter string; if empty, all
     *                             subscriptions for that subscriber will be
     *                             removed
     */
    static void unsubscribe(std::string group_name,
            std::string subscriber_name, std::string filter = "");

private :
    /** Constructor
     *
     * Create a new group `group_name`, and run its worker on `executor`. If
     * `executor` is `nullptr`, its worker will run on an arbitrarily chosen
     * executor.
     *
     * \param group_name [in] Name of group to create
     * \param executor   [in] Executor to run the group's worker on; may be
     *                        `nullptr`, in which case the worker will run on an
     *                        arbitrarily chosen executor
     *
     * \throw `duplicate_error` if a group or a worker with the same name
     *        already exists.
     */
    group_t(std::string group_name, executor_t* executor = nullptr);

    /** Forward the given `msg` to all interested subscribers
     *
     * \param msg [in] Message to forward
     */
    void forward(std::unique_ptr<msg_t> msg);

    std::string name_;

    typedef std::unique_lock<ft::safe_mutex<std::mutex>> safe_lock_t;
    ft::safe_mutex<std::mutex> mutex_;

    /** Has this group been explicitely created? */
    bool is_explicit;

    /** All subscriptions of a given subscriber
     *
     * The key is the filter string, the value the regex to apply.
     */
    typedef std::map<std::string, std::regex> subscriptions_t;

    /** Subscribers of a given group */
    typedef std::unordered_map<std::string, subscriptions_t> subscribers_t;

    /** All subscribers for this group */
    subscribers_t subscribers_;

    /** Remove the given subscriber from the group */
    void remove_subscriber(subscribers_t::iterator& it);
};

} // namespace skal
