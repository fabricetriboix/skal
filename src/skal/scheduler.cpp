/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <internal/policy.hpp>
#include <internal/queue.hpp>
#include <skal/worker.hpp>
#include <skal/detail/semaphore.hpp>
#include <list>
#include <mutex>
#include <memory>
#include <utility>
#include <boost/optional.hpp>

namespace skal {
namespace {

struct work_t
{
    int priority;
    queue_t queue;
    process_msg_t process_msg;

    work_t(const worker_t& params, queue_t::ntf_t ntf)
        : priority(params.priority)
        , queue(params.queue_threshold, ntf)
        , process_msg(params.process_msg)
    {
    }
};

class carousel_policy_t : public policy_interface_t
{
    typedef std::unique_lock<std::mutex> lock_t;
    typedef std::list<std::unique_ptr<work_t>> workers_t;
    mutable std::mutex mutex_;
    workers_t workers_;
    boost::optional<workers_t::iterator> iterator_;
    ft::semaphore_t semaphore_;

public :
    carousel_policy_t() = default;

    void add(std::unique_ptr<work_t> worker) override
    {
        params.check();
        lock_t lock(mutex_);
        workers_.push_back(std::move(worker));
    }

    bool run_one() override
    {
        lock_t lock(mutex_);
        semaphore_.take();
        if (workers_t.empty()) {
            return false;
        }
        workers_t::iterator = next();
        std::unique_ptr<msg_t> msg = iterator->queue.pop();
        try {
            next->process_msg(
    }

private :
};

} // unnamed namespace

} // namespace skal
