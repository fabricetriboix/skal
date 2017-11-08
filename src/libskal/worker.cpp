/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/detail/log.hpp>
#include <mutex>
#include <unordered_map>
#include <algorithm>
#include <utility>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;
typedef std::unordered_map<std::string, worker_t*> workers_t;
workers_t g_workers;
std::mutex g_mutex;

} // unnamed namespace

worker_t::ptr_t worker_t::create(std::string name, process_t process,
        int64_t queue_threshold, std::chrono::nanoseconds xoff_timeout)
{
    lock_t lock(g_mutex);
    if (g_workers.find(name) != g_workers.end()) {
        throw duplicate_error();
    }
    // NB: Can't use `make_unique` here because constructor is private
    ptr_t worker = ptr_t(new worker_t(name, process,
                queue_threshold, xoff_timeout));
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

bool worker_t::post(msg_t::ptr_t& msg)
{
    lock_t lock(g_mutex);
    workers_t::iterator it = g_workers.find(msg->recipient());
    if (it == g_workers.end()) {
        return false;
    }
    it->second->queue_.push(std::move(msg));
    // TODO: throttling
    return true;
}

} // namespace skal
