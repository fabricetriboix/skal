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

#ifndef SKAL_BLOB_h_
#define SKAL_BLOB_h_

#include "skal.h"



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise SKAL allocators for this process
 *
 * The "malloc" and "shm" allocator will be automatically created.
 *
 * @param allocators [in] Array of custom blob allocators; may be NULL if you
 *                        don't have custom allocators
 * @param size       [in] Size of the previous array
 */
void SkalBlobInit(const SkalAllocator* allocators, int size);


/* Deregister all allocators for this process */
void SkalBlobExit(void);


#endif /* SKAL_BLOB_h_ */
