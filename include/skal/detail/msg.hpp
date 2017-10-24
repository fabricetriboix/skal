/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>

namespace skal {

struct iflag_t final
{
    /** Internal message flag: this is an internal message */
    constexpr static uint32_t internal = 0x10000;
};

/** Get the domain this process belongs to */
const std::string& domain();

/** Set the domain this process belongs to
 *
 * \param domain [in] Domain this process belongs to
 */
void domain(std::string domain);

} // namespace skal
