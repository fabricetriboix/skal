/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/worker.hpp>
#include <thread>
#include <mutex>
#include <map>
#include <boost/thread/thread.hpp>
#include <boost/date_time/posix_time/posix_time.hpp>

namespace skal {

/** Name of this process */
std::string g_process_name;

/** Xoff item
 *
 * This structure keeps track of another worker that has been blocked by myself
 * because it was sending too fast.
 */
struct xoff_t final
{
    /** Name of the recipient that got its queue full because of me */
    std::string peer;

    /** Last time we sent a "skal-ntf-xon" message to `peer` */
    boost::posix_time::ptime last_ntf_xon;
};

/** Class that defines a worker
 *
 */
class worker_t final
{
};

/** Mutex to protect the map of workers */
std::mutex g_mutex;

/** Map of workers */
std::map<std::string, worker_t> g_workers;

void create_worker(std::string name, processor_t processor,
        const worker_params_t& params)
{
    // TODO
}

void create_worker(std::string name, processor_t processor)
{
    create_worker(std::move(name), processor, worker_params_t());
}

bool pause()
{
    // TODO
    return false;
}

void cancel_pause()
{
    // TODO
}

} // namespace skal
