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

#ifndef SKAL_THREAD_h_
#define SKAL_THREAD_h_

#ifdef __cplusplus
extern "C" {
#endif


#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Opaque type to a thread */
typedef struct SkalThread SkalThread;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise the thread module & create the master thread
 *
 * @param skaldUrl [in] skal-net style URL to skald socket, or NULL for default
 *
 * @return `true` if OK, `false` if it can't connect to skald
 */
bool SkalThreadInit(const char* skaldUrl);


/** Terminate all threads and free up all resources
 *
 * This function will block until all threads have terminated and all resources
 * have been freed. The master thread will also be terminated.
 */
void SkalThreadExit(void);


/** Pause the calling thread until all threads have exited
 *
 * @return `true` if all threads have terminated, `false` if
 *         `SkalThreadCancel()` has been called
 */
bool SkalThreadPause(void);


/** Cancel a `SkalThreadPause()` */
void SkalThreadCancel(void);



#ifdef __cplusplus
}
#endif


#endif /* SKAL_THREAD_h_ */
