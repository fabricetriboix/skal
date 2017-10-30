/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "internal/domain.hpp"
#include <skal/detail/log.hpp>

namespace skal {

namespace {

/** Domain this process belongs to
 *
 * NB: We don't bother protecting it with a mutex, because it is set once at
 *     the beginning (from the main thread and with no worker being created
 *     yet) and never modified again afer that.
 */
std::string g_domain = "skal-standalone";

} // unnamed namespace

const std::string& domain()
{
    return g_domain;
}

void domain(std::string domain)
{
    g_domain = std::move(domain);
}

std::string worker_name(std::string name)
{
    if (name.empty()) {
        return std::string();
    }
    if (name.find('@') != std::string::npos) {
        return std::move(name);
    }
    return name + '@' + g_domain;
}

} // namespace skal
