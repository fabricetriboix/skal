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

#ifndef SKAL_CFG_h_
#define SKAL_CFG_h_

#ifdef __cplusplus
extern "C" {
#endif


/** SKAL compile-time configuration
 *
 * @defgroup skalcfg SKAL Configuration
 * @addtogroup skalcfg
 * @{
 *
 * This file defines all the compile-time configuration parameters for SKAL.
 */



/** Default TTL */
#define SKAL_DEFAULT_TTL 4


/** Default XOFF timeout (how long to wait before retrying to send) */
#define SKAL_DEFAULT_XOFF_TIMEOUT_us 50000


/** Default queue threshold
 *
 * This is also the value that is used by the skal-master threads.
 */
#define SKAL_DEFAULT_QUEUE_THRESHOLD 100


/** Default skal-net style URL to connect to skald */
#define SKAL_DEFAULT_SKALD_URL "unix:///tmp/skald.sock"


/** Default backlog value for server sockets */
#define SKAL_NET_DEFAULT_BACKLOG 20


/** Default timeout value for connectionless sockets */
#define SKAL_NET_DEFAULT_TIMEOUT_us (10 * 1000 * 1000)


/** Socket polling timeout
 *
 * Please note this affects only how often connectionless sockets are checked
 * for termination.
 */
#define SKAL_NET_POLL_TIMEOUT_us (10 * 1000)


/** Default socket buffer size, in bytes */
#define SKAL_NET_DEFAULT_BUFSIZE_B (128 * 1024)


/** Minimum socket buffer size, in bytes */
#define SKAL_NET_MIN_BUFSIZE_B 2048


/** Maximum socket buffer size, in bytes */
#define SKAL_NET_MAX_BUFSIZE_B 212992


/** Permissions for shared memory files
 *
 * These are set to read/write for both the creator of the shared memory file
 * and the group, but no access to others.
 */
#define SKAL_SHM_PERM (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP)


/** Default MTU: standard ethernet frame, less a few bytes for VLAN id, etc. */
#define SKAL_PROTO_DEFAULT_MTU_B 1472


/** Default retransmit timeout */
#define SKAL_PROTO_DEFAULT_RETRANSMIT_TIMEOUT_ms 5


/** Default send queue size */
#define SKAL_PROTO_DEFAULT_SEND_QUEUE_SIZE 5


/** Default output bitrate (50Mbps) */
#define SKAL_PROTO_DEFAULT_BITRATE_bps 50000000


/** Default output burst rate (10 packets at default MTU size) */
#define SKAL_PROTO_DEFAULT_BURST_RATE_bsp (10 * SKAL_PROTO_DEFAULT_MTU_B * 8)



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKAL_CFG_h_ */
