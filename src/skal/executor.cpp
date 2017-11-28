/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/executor.hpp>
#include <skal/log.hpp>
#include <deque>
#include <mutex>

namespace skal {

namespace {

typedef std::unique_lock<std::mutex> lock_t;
std::deque<executor_t*> g_executors;
std::mutex g_mutex;
size_t g_next_executor = 0;

} // unnamed namespace

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

    // Add this executor to the executor register
    lock_t lock(g_mutex);
    skal_log(debug) << "Adding executor " << this << " to the register";
    g_executors.push_back(this);
}

executor_t::~executor_t()
{
    // Remove this executor from the executor register
    {
        lock_t lock(g_mutex);
        for (std::deque<executor_t*>::iterator it = g_executors.begin();
                it != g_executors.end(); ++it) {
            if (*it == this) {
                skal_log(debug) << "Removing executor " << this
                    << " from the register";
                g_executors.erase(it);
                break;
            }
        }
    }

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

executor_t* executor_t::get_arbitrary_executor()
{
    lock_t lock(g_mutex);
    skal_assert(g_executors.size() > 0);
    if (g_next_executor >= g_executors.size()) {
        g_next_executor = 0;
    }
    size_t n = g_next_executor;
    ++g_next_executor;
    executor_t* executor = g_executors[n];
    skal_log(debug) << "get_arbitrary_executor() returned executor "
        << executor << " (index " << n << ")";
    return executor;
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
    for (;;) {
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
        io_service_.post(
                [this, worker = std::move(worker)] ()
                {
                    this->run_one(std::move(worker));
                });
    } // infinite loop
}

void executor_t::run_one(std::shared_ptr<worker_t> worker)
{
    skal_assert(worker);
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
    }
}

} // namespace skal
