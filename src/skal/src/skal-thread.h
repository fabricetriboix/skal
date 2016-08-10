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

#ifndef SKAL_THREAD_h_
#define SKAL_THREAD_h_

#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Opaque type to a thread */
typedef struct SkalThread SkalThread;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise the thread module & create the master thread */
void SkalThreadInit(void);


/** Terminate all threads and free up all resources
 *
 * This function will block until all threads have terminated and all resources
 * have been freed. The master thread will also be terminated.
 */
void SkalThreadExit(void);



#endif /* SKAL_THREAD_h_ */
