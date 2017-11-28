/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/msg.hpp>

namespace skal {

void send_to_skald(std::unique_ptr<msg_t> msg);

} // namespace skal
