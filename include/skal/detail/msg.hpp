/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>

namespace skal {

struct iflag_t final
{
    /** Internal message flag: this is an internal message */
    constexpr static uint32_t internal = 0x10000;
};

} // namespace skal
