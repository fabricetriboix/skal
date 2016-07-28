/* Copyright (c) 2016  Fabrice Triboix
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

/** SKAL compile-time configuration
 *
 * \defgroup skalcfg SKAL Configuration
 * \addtogroup skalcfg
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


/** Maximum number of custom allocators */
#define SKAL_ALLOCATORS_MAX 500


/** Maximum number of fields per message */
#define SKAL_FIELDS_MAX 1000


/** Maximum number of threads per process */
#define SKAL_THREADS_MAX 10000


/** Maximum number of messages that can be queued in a message list */
#define SKAL_MSG_LIST_MAX 1000


/** Maximum number of xoff states that can be received by a single thread */
#define SKAL_XOFF_MAX 1000



/* @} */
#endif /* SKAL_CFG_h_ */
