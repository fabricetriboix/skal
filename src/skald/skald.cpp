/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "skald.hpp"
#include <vector>
#include <thread>
#include <boost/asio.hpp>

namespace skald {

namespace {

boost::asio::io_service g_io_service;
boost::asio::io_service::work g_work(g_io_service);
std::vector<std::thread> g_io_threads;

} // unnamed namespace

void init(parameters_t params)
{
    skal_log(info) << "Initialising skald";
    skal_assert(params.nthreads > 0);
    for (int i = 0; i < params.nthreads; ++i) {
        g_io_threads.push_back(std::thread(
                    [] ()
                    {
                        g_io_service.run();
                    }));
    }
    // TODO: grep TCP server socket
    skal_log(notice) << "skald is now running";
}

void terminate()
{
    skal_log(info) << "Terminating skald";
    g_io_service.stop();
    for (auto& t : g_io_threads) {
        t.join();
    }
    skal_log(notice) << "skald terminated";
}

} // namespace skald
