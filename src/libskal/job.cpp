/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <internal/job.hpp>
#include <mutex>
#include <unordered_map>
#include <algorithm>

namespace skal {

namespace {

typedef std::unordered_map<std::string, job_t*> jobs_t;
jobs_t g_jobs;
std::mutex g_mutex;

} // unnamed namespace

job_t::lock_t::lock_t() : lock_(g_mutex)
{
}

job_t::lock_t job_t::get_lock()
{
    return lock_t();
}

void job_t::add(const std::string& worker_name, job_t* job)
{
    skal_assert(job != nullptr);
    if (lookup(worker_name) != nullptr) {
        throw duplicate_error();
    }
    g_jobs[worker_name] = job;
}

void job_t::remove(const std::string& worker_name)
{
    g_jobs.erase(worker_name);
}

job_t* job_t::lookup(const std::string& worker_name)
{
    jobs_t::iterator it = g_jobs.find(worker_name);
    if (it != g_jobs.end()) {
        return it->second;
    } else {
        return nullptr;
    }
}

} // namespace skal
