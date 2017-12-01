/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/msg.hpp>

namespace skal {

void net_init(const std::string& skald_url);

void send_to_skald(std::unique_ptr<msg_t> msg);

} // namespace skal
