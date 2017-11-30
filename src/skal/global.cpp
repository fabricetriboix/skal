/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/global.hpp>
#include <thread>

namespace skal {

namespace {

/** Domain this process belongs to
 *
 * NB: We don't bother protecting it with a mutex, because it is set once at
 *     the beginning (from the main thread and with no worker being created
 *     yet) and never modified again afer that.
 */
std::string g_domain = "skal-standalone";

/** Name of the worker's thread */
thread_local std::string g_me = "";

} // unnamed namespace

const std::string& domain()
{
    return g_domain;
}

const std::string& me()
{
    return g_me;
}

void global_t::set_domain(std::string domain)
{
    g_domain = std::move(domain);
}

void global_t::set_me(std::string me)
{
    g_me = full_name(std::move(me));
}

std::string full_name(std::string name)
{
    if (name.empty()) {
        return std::string();
    }
    if (name.find('@') != std::string::npos) {
        return std::move(name);
    }
    return name + '@' + g_domain;
}

bool is_standalone()
{
    return g_domain == "skal-standalone";
}

} // namespace skal
