/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include <skal/net.hpp>
#include <skal/error.hpp>

namespace skal {

void net_init(const std::string& skald_url)
{
    skal_panic() << "Networking not yet implemented";
}

void send_to_skald(std::unique_ptr<msg_t> msg)
{
    // TODO
}

} // namespace skal
