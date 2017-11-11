/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/skal.hpp>
#include <skal/detail/util.hpp>
#include <boost/asio.hpp>

namespace skal {

namespace {

boost::asio::io_service g_io_service;
std::vector<executor_t> g_executors;

} // unnamed namespace

void run_skal(const params_t& params, std::vector<executor_cfg_t> executor_cfgs,
        std::vector<worker_t> workers)
{
    skal_assert(!workers.empty());

    if (executor_cfgs.empty()) {
        executor_cfgs.push_back(executor_cfg_t { policy_t::biggest });
    }
    for (const auto& cfg : executor_cfgs) {
        g_executors.emplace_back(cfg.policy);
    }

    

    boost::asio::io_service::work work(g_io_service);
}

void terminate_skal()
{
    g_io_service.stop();
}

} // namespace skal
