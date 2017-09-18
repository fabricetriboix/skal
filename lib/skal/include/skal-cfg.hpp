/* Copyright (c) 2016,2017  Fabrice Triboix
 *
 * This program is free software: you can redistribute it and/or modify
 * it under the terms of the GNU General Public License as published by
 * the Free Software Foundation, either version 3 of the License, or
 * (at your option) any later version.
 *
 * This program is distributed in the hope that it will be useful,
 * but WITHOUT ANY WARRANTY; without even the implied warranty of
 * MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the
 * GNU General Public License for more details.
 *
 * You should have received a copy of the GNU General Public License
 * along with this program.  If not, see <http://www.gnu.org/licenses/>.
 */

#pragma once

#include <cstdint>
#include <string>
#include <chrono>

namespace skal {

/** SKAL compile-time configuration
 *
 * @defgroup skalcfg SKAL Configuration
 * @addtogroup skalcfg
 * @{
 *
 * This file defines all the compile-time configuration parameters for SKAL.
 */

/** Default TTL */
constexpr int8_t default_ttl = 4;

/** Default XOFF timeout (how long to wait before retrying to send) */
constexpr auto default_xoff_timeout = std::chrono::milliseconds(50);

/** Default queue threshold
 *
 * This is also the value that is used by the skal-master threads.
 */
constexpr int default_queue_threshold = 100;

/** Default URL to connect to skald */
constexpr std::string default_skald_url = "unix:///tmp/skald.sock";

/** Default timeout value for connectionless sockets */
constexpr std::chrono::milliseconds default_cnxless_timeout = 10 * 1000;

/** Default socket buffer size, in bytes */
constexpr int default_bufsize_B = 128 * 1024;

/** Minimum socket buffer size, in bytes */
constexpr int min_bufsize_B = 2048;

/** Maximum socket buffer size, in bytes */
constexpr int max_bufsize_B = 212992;

/** Default MTU: standard ethernet frame, less a few bytes for VLAN id, etc. */
constexpr int default_mtu_B = 1472;

/** Default retransmit timeout */
constexpr auto default_retransmit_timeout = std::chrono::milliseconds(5);

/** Default send queue size */
constexpr int default_send_queue_size = 5;

/** Default output bitrate (50Mbps) */
constexpr int default_bitrate_bps = 50 * 1000 * 1000;

/* @} */

} // namespace skal
