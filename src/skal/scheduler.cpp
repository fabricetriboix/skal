/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/scheduler.hpp>
#include <skal/worker.hpp>
#include <skal/error.hpp>
#include <list>
#include <memory>
#include <utility>
#include <algorithm>

namespace skal {

namespace {

class fair_scheduler_t final : public scheduler_t
{
    typedef std::list<std::shared_ptr<worker_t>> workers_t;
    workers_t workers_;

    workers_t::iterator lookup(const std::string& worker_name)
    {
        return std::find_if(workers_.begin(), workers_.end(),
                [&worker_name] (const std::shared_ptr<worker_t>& worker)
                {
                    return worker->name() == worker_name;
                });
    }

    void do_add(std::unique_ptr<worker_t> worker) override
    {
        skal_assert(worker);
        workers_t::iterator it = lookup(worker->name());
        skal_assert(it == workers_.end()) << "Duplicate worker name '"
            << worker->name() << "'";
        workers_.push_back(std::move(worker));
    }

    void do_remove(const std::string& worker_name) override
    {
        workers_t::iterator it = lookup(worker_name);
        if (it != workers_.end()) {
            workers_.erase(it);
        }
    }

    bool do_is_empty() const override
    {
        return workers_.empty();
    }

    std::shared_ptr<worker_t> do_select() const override
    {
        std::shared_ptr<worker_t> selected;
        for (auto& worker : workers_) {
            if (worker->blocked()) {
                if (worker->internal_msg_count() > 0) {
                    // Empty blocked workers' internal messages first
                    return worker;
                }
            } else if (!selected && (worker->msg_count() > 0)) {
                selected = worker;
            } else if (worker->msg_count() > selected->msg_count()) {
                selected = worker;
            }
        } // for each worker
        return selected;
    }

public :
    ~fair_scheduler_t() = default;
    fair_scheduler_t() = default;
};

} // unnamed namespace

std::unique_ptr<scheduler_t> create_scheduler(policy_t policy)
{
    skal_assert(policy == policy_t::fair);
    return std::make_unique<fair_scheduler_t>();
}

} // namespace skal
