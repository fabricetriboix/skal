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



/** Maximum length of names or other strings, in chars
 *
 * That includes the terminating null character. This setting essentially
 * applies to all strings unless otherwise specified.
 */
#define SKAL_NAME_MAX 128


/** Maximum length of a thread name, in chars
 *
 * That includes the terminated null character. Do not modify.
 */
#define SKAL_THREAD_NAME_MAX (SKAL_NAME_MAX / 2)


/** Maximum length of a domain name, in chars
 *
 * That includes the terminated null character. Do not modify.
 */
#define SKAL_DOMAIN_NAME_MAX (SKAL_NAME_MAX / 2)


/** Maximum number of custom allocators */
#define SKAL_ALLOCATORS_MAX 500


/** Maximum number of threads per process */
#define SKAL_THREADS_MAX 10000


/** Maximum number of fields per message */
#define SKAL_FIELDS_MAX 1000


/** Maximum number of messages that can be queued in a message list */
#define SKAL_MSG_LIST_MAX 1000


/** Maximum number of xoff states that can be received by a single thread */
#define SKAL_XOFF_MAX 1000


/** Default TTL */
#define SKAL_DEFAULT_TTL 4


/** Default XOFF timeout (how long to wait before retrying to send) */
#define SKAL_DEFAULT_XOFF_TIMEOUT_us 50000


/** Default queue threshold */
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



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKAL_CFG_h_ */
