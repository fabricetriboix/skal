/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <skal/cfg.hpp>
#include <internal/domain.hpp>

namespace skal {

worker_t::worker_t(std::string name, process_msg_t process)
    : name(worker_name(name))
    , process_msg(process)
    , priority(0)
    , queue_threshold(default_queue_threshold)
    , xoff_timeout(default_xoff_timeout)
{
}

} // namespace skal
