/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/skal.hpp>
#include <skal/detail/util.hpp>
#include <boost/asio.hpp>

namespace skal {

namespace {

boost::asio::io_service g_io_service;

} // unnamed namespace

void run_skal(std::string process_name, std::string skald_url,
        std::vector<worker_params_t> workers,
        std::vector<exec_cfg_t> exec_cfgs)
{
    url_t url(skald_url);

    if (url.scheme() == "tcp") {
        boost::asio::ip::tcp::resolver resolver(g_io_service);
        if (url.host().empty() || url.port().empty()) {
            throw bad_url();
        }
        boost::asio::ip::tcp::resolver::query query(url.host(), port);
        // TODO: from here
        boost::asio::ip::tcp::resolver::iterator it
    }

    boost::asio::io_service::work work(g_io_service);
}

void terminate_skal()
{
    // TODO
    g_io_service.stop();
}

} // namespace skal
