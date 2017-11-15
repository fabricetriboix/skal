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
public :
    typedef std::list<std::unique_ptr<worker_t>> workers_t;

    ~fair_scheduler_t() = default;
    fair_scheduler_t() = default;

    void add(std::unique_ptr<worker_t> worker) override
    {
        skal_assert(worker);
        workers_t::iterator it = lookup(worker->name());
        skal_assert(it == workers_.end()) << "Duplicate worker name '"
            << worker->name() << "'";
        workers_.push_back(std::move(worker));
    }

    void remove(const std::string& worker_name) override
    {
        workers_t::iterator it = lookup(worker_name);
        if (it != workers_.end()) {
            workers_.erase(it);
        }
    }

    worker_t* select() override
    {
        worker_t* selected = nullptr;
        for (auto& worker : workers_) {
            if (worker->blocked()) {
                if (worker->internal_msg_count() > 0) {
                    return worker.get();
                }
            } else if ((selected == nullptr) && (worker->msg_count() > 0)) {
                selected = worker.get();
            } else if (worker->msg_count() > selected->msg_count()) {
                selected = worker.get();
            }
        } // for each worker
        return selected;
    }

private :
    workers_t workers_;

    workers_t::iterator lookup(const std::string& worker_name)
    {
        return std::find_if(workers_.begin(), workers_.end(),
                [&worker_name] (const std::unique_ptr<worker_t>& worker)
                {
                    return worker->name() == worker_name;
                });
    }
};

} // unnamed namespace

std::unique_ptr<scheduler_t> create_scheduler(policy_t policy)
{
    skal_assert(policy == policy_t::fair);
    return std::make_unique<fair_scheduler_t>();
}

} // namespace skal
