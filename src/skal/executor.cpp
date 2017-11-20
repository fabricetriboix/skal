/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/executor.hpp>
#include <skal/log.hpp>

namespace skal {

executor_t::executor_t(std::unique_ptr<scheduler_t> scheduler, int nthreads)
    : is_terminated_(false)
    , scheduler_(std::move(scheduler))
    , work_(io_service_)
    , dispatcher_thread_( [this] () { this->run_dispatcher(); } )
{
    skal_assert(scheduler_);
    skal_assert(nthreads > 0);
    for (int i = 0; i < nthreads; ++i) {
        threads_.emplace_back(
                [this] ()
                {
                    io_service_.run();
                });
    }
}

executor_t::~executor_t()
{
    // Terminate dispatcher thread
    is_terminated_ = true;
    semaphore_.post();
    dispatcher_thread_.join();

    // Terminate thread pool
    io_service_.stop();
    for (auto& thread : threads_) {
        thread.join();
    }

    // De-allocate all workers
    scheduler_.reset();
}

void executor_t::add_worker(std::unique_ptr<worker_t> worker)
{
    skal_log(debug) << "Adding worker '" << worker->name() <<
        "' to executor " << this;
    worker->listen(
            [this] ()
            {
                this->semaphore_.post();
            });
    scheduler_->add(std::move(worker));
}

void executor_t::run_dispatcher()
{
    for (;;)
    {
        semaphore_.take();
        if (is_terminated_) {
            break;
        }
        std::shared_ptr<worker_t> worker = scheduler_->select();
        if (!worker) {
            // May happen if a worker terminates before processing that msg
            skal_log(debug)
                << "I received a signal that a message has arrived, but there is no worker with pending messages";
            continue;
        }

        bool terminated = false;
        try {
            terminated = worker->process_one();
            if (terminated) {
                skal_log(debug) << "Worker '" << worker->name()
                    << "' terminated cleanly";
            }
        } catch (std::exception& e) {
            skal_log(notice) << "Worker '" << worker->name()
                << "' threw an exception: " << e.what()
                << " - worker is now terminated";
            terminated = true;
        } catch (...) {
            skal_log(notice) << "Worker '" << worker->name()
                << "' threw a non-standard exception - worker is now terminated";
            terminated = true;
        }

        if (terminated) {
            scheduler_->remove(worker->name());
            worker.reset();
            if (scheduler_->is_empty()) {
                skal_log(info) << "Last worker terminated for this executor, "
                    << "terminating this executor";
                break;
            }
        }
    } // infinite loop
}

} // namespace skal
