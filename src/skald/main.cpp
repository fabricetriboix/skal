/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "skald.hpp"
#include <iostream>
#include <cstring>
#include <csignal>
#include <cerrno>
#include <unistd.h>
#include <boost/program_options.hpp>

namespace po = boost::program_options;

namespace skald {

namespace {

enum class state_t {
    starting,
    running,
    terminating,
};

state_t g_state = state_t::starting;

void handle_signal(int signum)
{
    switch (g_state) {
    case state_t::starting :
        skal_log(notice) << "Received signal " << signum
            << ", but skald has not started yet; forcing termination";
        std::terminate();
        break;

    case state_t::running :
        skal_log(notice) << "Received signal " << signum
            << ", terminating... (send signal again to force termination)";
        g_state = state_t::terminating;
        break;

    case state_t::terminating :
        skal_log(notice) << "Received signal " << signum
            << " again, forcing termination now";
        std::terminate();
        break;

    default :
        skal_panic() << "Unknown state " << static_cast<int>(g_state);
        break;
    }
}

bool parse_args(int& argc, char**& argv, parameters_t& params)
{
    po::options_description desc("Run the skald daemon");
    std::string verbose;
    desc.add_options()
        ("help,h", "Print usage message")
        ("verbose,v", po::value(&verbose)->implicit_value(""),
            "Be more verbose (can be cumulated)")
        ("domain,d", po::value<std::string>(), "Set the skald domain")
        ("local,l", po::value<std::string>(), "Local URL to listen to");
    po::variables_map vars;
    po::store(po::parse_command_line(argc, argv, desc), vars);
    po::notify(vars);

    if (vars.count("help")) {
        std::cout << desc << std::endl;
        return false;
    }
    if (vars.count("domain")) {
        params.domain = vars["domain"].as<std::string>();
    }
    if (vars.count("local")) {
        params.local_url = vars["local"].as<std::string>();
    }
    if (vars.count("verbose")) {
        verbose += "v";
    }
    if (verbose.size() > 1) {
        skal::log::minimum_level(skal::log::level_t::debug);
    } else if (verbose.size() > 0) {
        skal::log::minimum_level(skal::log::level_t::info);
    }
    return true;
}

} // unnamed namespace

} // namespace skald

int main(int argc, char** argv)
{
    struct sigaction sa;
    std::memset(&sa, 0, sizeof(sa));
    sa.sa_handler = skald::handle_signal;
    int ret = ::sigaction(SIGINT, &sa, nullptr);
    if (ret < 0) {
        skal_log(error) << "sigaction(SIGINT) failed: "
            << std::strerror(errno) << " [" << errno << "]";
        return 1;
    }
    ret = ::sigaction(SIGTERM, &sa, nullptr);
    if (ret < 0) {
        skal_log(error) << "sigaction(SIGTERM) failed: "
            << std::strerror(errno) << " [" << errno << "]";
        return 1;
    }

    skald::parameters_t params;
    if (!skald::parse_args(argc, argv, params)) {
        return 1;
    }

    skald::init(params);

    skald::g_state = skald::state_t::running;
    while (skald::g_state == skald::state_t::running) {
        ::pause();
    }

    skald::terminate();
    return 0;
}
