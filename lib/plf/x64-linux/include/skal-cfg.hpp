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
#include <boost/chrono.hpp>


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
const int8_t DEFAULT_TTL(4);


/** Default XOFF timeout (how long to wait before retrying to send) */
const boost::chrono::milliseconds DEFAULT_XOFF_TIMEOUT(50);


/** Default queue threshold
 *
 * This is also the value that is used by the skal-master threads.
 */
const int DEFAULT_QUEUE_THRESHOLD(100);


/** Default URL to connect to skald */
const std::string DEFAULT_SKALD_URL("unix:///tmp/skald.sock");


/** Default timeout value for connectionless sockets */
const boost::chrono::milliseconds DEFAULT_CNXLESS_TIMEOUT(10 * 1000);


/** Default socket buffer size, in bytes */
const int DEFAULT_BUFSIZE_B(128 * 1024);


/** Minimum socket buffer size, in bytes */
const int MIN_BUFSIZE_B(2048);


/** Maximum socket buffer size, in bytes */
const int MAX_BUFSIZE_B(212992);


/** Default MTU: standard ethernet frame, less a few bytes for VLAN id, etc. */
const int DEFAULT_MTU_B(1472);


/** Default retransmit timeout */
const boost::chrono::milliseconds DEFAULT_RETRANSMIT_TIMEOUT(5);


/** Default send queue size */
const int DEFAULT_SEND_QUEUE_SIZE(5);


/** Default output bitrate (50Mbps) */
const int DEFAULT_BITRATE_bps(50 * 1000 * 1000);


/* @} */

} // namespace skal
