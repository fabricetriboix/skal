/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/worker.hpp>
#include <internal/policy.hpp>
#include <memory>
#include <list>
#include <mutex>
#include <thread>
#include <boost/noncopyable.hpp>

namespace skal {

class executor_t final : boost::noncopyable
{
public :
    executor_t() = delete;
    ~executor_t();

    /** Constructor
     *
     * \param cpu [in] CPU to run this executor on
     */
    executor_t(int cpu, std::unique_ptr<policy_interface_t> policy);

    /** Add a worker to be executed on this executor
     *
     * \param params [in] Worker's parameters
     */
    void add(const worker_t& params);

private :
    mutable std::mutex mutex_;
    typedef std::unique_lock<std::mutex> lock_t;
    std::thread thread_;
    std::unique_ptr<policy_interface_t> policy_;
};

} // namespace skal
