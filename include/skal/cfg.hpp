/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <cstdint>
#include <string>
#include <chrono>

using namespace std::literals::chrono_literals;

namespace skal {

/** Defines a moment in time */
typedef std::chrono::system_clock::time_point timepoint_t;

/** SKAL compile-time configuration
 *
 * @defgroup skalcfg SKAL Configuration
 * @addtogroup skalcfg
 * @{
 *
 * This file defines all the compile-time configuration parameters for SKAL.
 */

/** Default log level: one of the `skal::log:level_t` */
#define SKAL_DEFAULT_LOG_LEVEL debug

/** Default TTL */
constexpr int8_t default_ttl = 4;

/** Default XOFF timeout (how long to wait before retrying to send) */
constexpr auto default_xoff_timeout = 50ms;

/** Default queue threshold
 *
 * This is also the value that is used by the skal-master threads.
 */
constexpr int default_queue_threshold = 100;

/** Default URL to connect to skald */
constexpr const char* default_skald_url = "unix:///tmp/skald.sock";

/** Default timeout value for connectionless sockets */
constexpr auto default_cnxless_timeout = 10s;

/** Default socket buffer size, in bytes */
constexpr int default_bufsize_B = 128 * 1024;

/** Minimum socket buffer size, in bytes */
constexpr int min_bufsize_B = 2048;

/** Maximum socket buffer size, in bytes */
constexpr int max_bufsize_B = 212992;

/** Default MTU: standard ethernet frame, less a few bytes for VLAN id, etc. */
constexpr int default_mtu_B = 1472;

/** Default retransmit timeout */
constexpr auto default_retransmit_timeout = 5ms;

/** Default send queue size */
constexpr int default_send_queue_size = 5;

/** Default output bitrate (50Mbps) */
constexpr int default_bitrate_bps = 50 * 1000 * 1000;

/* @} */

} // namespace skal
