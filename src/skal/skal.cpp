/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/skal.hpp>
#include <skal/net.hpp>
#include <skal/util.hpp>

namespace skal {

void init(const parameters_t& parameters)
{
    if (!parameters.standalone) {
        net_init(parameters.skald_url);
    }
}

void wait()
{
    worker_t::wait();
}

void terminate()
{
    worker_t::terminate();
}

} // namespace skal
