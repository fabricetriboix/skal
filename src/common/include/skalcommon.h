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

#ifndef SKAL_COMMON_h_
#define SKAL_COMMON_h_

/** Platform-dependent stuff for SKAL
 *
 * \defgroup skalcommon Platform-dependent stuff for SKAL
 * \addtogroup skalcommon
 * @{
 */

#include "skalplf.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Maximum length of any name, including the terminating null char, in chars */
#define SKAL_NAME_MAX 128


/** Minimum ASCII character */
#define SKAL_ASCII_MIN ((char)0x20)


/** Maximum ASCII character */
#define SKAL_ASCII_MAX ((char)0x7e)



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** `malloc()` replacement
 *
 * The behaviour of this function is the same as for `malloc(3)`, but it asserts
 * if it fails.
 *
 * \param size_B [in] Number of bytes to allocate; must be > 0
 *
 * \return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalMalloc(int size_B);


/** Allocate memory and initialises it to zero
 *
 * The behaviour of this function is the same as for `malloc(3)`, but it asserts
 * if it fails. Also, the memory will be initialised to zeros.
 *
 * \param size_B [in] Number of bytes to allocate; must be > 0
 *
 * \return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalMallocZ(int size_B);


/** `realloc()` replacement
 *
 * The behaviour of this function is the same as for `realloc(3)`, but it
 * asserts if it fails.
 *
 * \param ptr    [in,out] Pointer to the memory area to replace; may be NULL
 * \param size_B [in]     Number of bytes to re-allocate; must be > 0
 *
 * \return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalRealloc(void* ptr, int size_B);


/** `calloc()` replacement
 *
 * The behaviour of this function is the same as for `calloc(3)`, but it asserts
 * if it fails. The memory will be initialised to zeros.
 *
 * \param nItems     [in] Number of items to allocate; must be > 0
 * \param itemSize_B [in] Size of one item, in bytes; must be > 0
 *
 * \return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalCalloc(int nItems, int itemSize_B);


/** Helper function to sprintf a string
 *
 * \param format [in] A printf-like format string
 * \param ...    [in] Printf-like arguments
 *
 * \return The formatted string, never NULL; please release with `free()` when
 *         you no longer need it
 */
char* SkalSPrintf(const char* format, ...)
    __attribute__(( format(printf, 1, 2) ));


/** Check that the given string is a pure ASCII string with a null char
 *
 * \param str    [in] String to check; must not be NULL
 * \param maxlen [in] Maximum string length, in characters; must be > 0
 *
 * \return `true` if the string contains only printable ASCII characters and has
 *         a terminated null characters within the first `maxlen` characters
 */
bool SkalIsAsciiString(const char* str, int maxlen);



/* @} */
#endif /* SKAL_COMMON_h_ */
