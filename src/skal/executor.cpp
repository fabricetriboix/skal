/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/executor.hpp>
#include <skal/log.hpp>

namespace skal {

executor_t::executor(std::unique_ptr<scheduler_t> scheduler)
    : work_(io_service_)
    , dispatcher_thread_( [this] () { this->run_dispatcher(); } )
{
    for (int i = 0; i < 3; ++i) {
        threads_.emplace_back( [this] () { io_service_.run(); } )
    }
}

executor_t::~executor_t()
{
    is_terminated_ = true;
    semaphore_.post();
    dispatcher_thread_.join();

    io_service_.stop();
    for (int i = 0; i < 3; ++i) {
        threads_[i].join();
    }
}

void executor_t::run_dispatcher()
{
    for (;;)
    {
        semaphore_.take();
        if (is_terminated_) {
            break;
        }
        lock_t lock(mutex_);
        // TODO: from here
    }
}

} // namespace skal
