/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <internal/job.hpp>
#include <internal/domain.hpp>
#include <mutex>
#include <unordered_map>
#include <memory>
#include <algorithm>

namespace skal {

namespace {


} // unnamed namespace

executor_t::executor_t(policy_t policy) : thread_([this] () { this->run(); })
{
}

executor_t::~executor_t()
{
    // TODO
}

void executor_t::run()
{
    for (;;) {
        semaphore_.take();

        // Check if we added a worker
        {
            lock_t lock(mutex_);
            if (!workers_to_add_.empty()) {
                create_job(workers_to_add_.front());
                workers_to_add_.pop_front();
                continue;
            }
        }

        // Otherwise, a job has received a message
        // TODO: implement xon/xoff
        for (auto& job : jobs_) {
            if (!job->queue.is_empty()) {
                msg_ptr_t msg = job->queue.pop();
                skal_assert(msg);
                job->process_msg(std::move(msg));
                break;
            }
        }
    } // thread loop
}

void executor_t::create_job(worker_t worker)
{
    worker.name = worker_name(worker.name);

    // Create the job and pushes the very first message: "skal-init"
    std::unique_ptr<job_t> job = std::make_unique<job_t>(worker,
            [this] () {
                this->semaphore.post();
            });
    job->queue.push(msg_t::create(worker_name("skal-executor"),
                worker.name, "skal-init"));

    // Add job to global job structure
    job_t::lock_t job_lock(job_t::get_lock());
    job_t::add(worker.name, job.get());
    jobs_.push_back(std::move(job));

    // Sort the list of jobs from higher priority to lower priority
    jobs_.sort(
            [] (const std::unique_ptr<job_t>& left,
                const std::unique_ptr<job_t>& right)
            {
                // NB: The ">" below is correct, it is not "<" because we
                // want the higher priority first.
                return left->priority > right->priority;
            });
}

} // namespace skal
