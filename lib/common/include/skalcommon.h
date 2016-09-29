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

/** Common stuff for SKAL
 *
 * @defgroup skalcommon Common stuff for SKAL
 * @addtogroup skalcommon
 * @{
 */

#include "skalplf.h"



/*------------------+
 | Macros and types |
 +------------------*/


/** Opaque type to a string builder
 *
 * A string builder allows building a string piece by piece, extending the
 * string when needed.
 *
 * First, call `SkalStringBuilderCreate()` to create a string builder. Then call
 * `SkalStringBuilderAppend()` as many times as you like to build the string
 * piece by piece. Finally, call `SkalStringBuilderFinish()` to finish the
 * building process.
 */
typedef struct SkalStringBuilder SkalStringBuilder;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** `malloc()` replacement
 *
 * The behaviour of this function is the same as for `malloc(3)`, but it asserts
 * if it fails.
 *
 * @param size_B [in] Number of bytes to allocate; must be > 0
 *
 * @return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalMalloc(int size_B);


/** Allocate memory and initialises it to zero
 *
 * The behaviour of this function is the same as for `malloc(3)`, but it asserts
 * if it fails. Also, the memory will be initialised to zeros.
 *
 * @param size_B [in] Number of bytes to allocate; must be > 0
 *
 * @return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalMallocZ(int size_B);


/** `realloc()` replacement
 *
 * The behaviour of this function is the same as for `realloc(3)`, but it
 * asserts if it fails.
 *
 * @param ptr    [in,out] Pointer to the memory area to replace; may be NULL
 * @param size_B [in]     Number of bytes to re-allocate; must be > 0
 *
 * @return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalRealloc(void* ptr, int size_B);


/** `calloc()` replacement
 *
 * The behaviour of this function is the same as for `calloc(3)`, but it asserts
 * if it fails. The memory will be initialised to zeros.
 *
 * @param nItems     [in] Number of items to allocate; must be > 0
 * @param itemSize_B [in] Size of one item, in bytes; must be > 0
 *
 * @return A pointer to the newly allocated memory area, never NULL; please
 *         release with `free()` when you no longer need it
 */
void* SkalCalloc(int nItems, int itemSize_B);


/** Helper function to sprintf a string
 *
 * @param format [in] A printf-like format string
 * @param ...    [in] Printf-like arguments
 *
 * @return The formatted string, never NULL; please release with `free()` when
 *         you no longer need it
 */
char* SkalSPrintf(const char* format, ...)
    __attribute__(( format(printf, 1, 2) ));


/** Create a string builder
 *
 * @param initialCapacity [in] Initial capacity of the string, or 0 for default
 *
 * @return The newly created string builder; this function never returns NULL
 */
SkalStringBuilder* SkalStringBuilderCreate(int initialCapacity);


/** Append to a string builder
 *
 * @param sb     [in,out] String builder to append to; must not be NULL
 * @param format [in]     A printf-like format string
 * @param ...    [in]     Printf-like arguments
 */
void SkalStringBuilderAppend(SkalStringBuilder* sb, const char* format, ...)
    __attribute__(( format(printf, 2, 3) ));


/** Cut some characters from the end of the string
 *
 * @param sb [in,out] String builder to modify; must not be NULL
 * @param n  [in]     Number of characters to trim from the end of the string
 *                    if <=0, no action is taken
 */
void SkalStringBuilderTrim(SkalStringBuilder* sb, int n);


/** Finish a string builder
 *
 * This function de-allocates the string builder, so you can't re-use it.
 *
 * @param sb [in,out] String builder to finish (it will be freed); must not be
 *                    NULL
 *
 * @return The resulting string; call `free()` on it when done with it
 */
char* SkalStringBuilderFinish(SkalStringBuilder* sb);


/** Check the given string is pure ASCII with a terminating null char
 *
 * @param str    [in] String to check; must not be NULL
 * @param maxlen [in] Maximum string length, in bytes; must be > 0
 *
 * @return `true` if the string contains only printable ASCII characters and has
 *         a terminating null character within the first `maxlen` bytes
 */
bool SkalIsAsciiString(const char* str, int maxlen);


/** Check the given string is UTF-8 with a terminating null char
 *
 * @param str    [in] String to check; must not be NULL
 * @param maxlen [in] Maximum string length, in bytes; must be > 0
 *
 * @return `true` if the string contains valid UTF-8 characters and has a
 *         terminating null character within the first `maxlen` bytes
 */
bool SkalIsUtf8String(const char* str, int maxlen);


/** Standard string comparison function suitable for CdsMap */
int SkalStringCompare(void* lefykey, void* rightkey, void* cookie);


/** Encode up to 3 bytes into 4 base64 characters
 *
 * *IMPORTANT* no null character will be added at the end.
 *
 * If `size_B` is <3, only the given number of bytes will be encoded.
 *
 * @param data       [in]  Pointer to bytes to encode; must not be NULL
 * @param size_B     [in]  Number of bytes to encode; must be >0
 * @param base64     [out] Encoded base64 characters; must not be NULL
 * @param base64Size [in]  Size of the above buffer, in chars; must be >=4
 *
 * @return The number of encoded bytes
 */
int SkalBase64Encode3(const uint8_t* data, int size_B,
        char* base64, int base64Size);


/** Encode binary data into a base64 string
 *
 * The output base64 string will be null-terminated.
 *
 * @param data   [in] Pointer to bytes to encode; must not be NULL
 * @param size_B [in] Number of bytes to encode; must be >0
 *
 * @return Encoded base64 string; this function never returns NULL; you must
 *         free the base64 string by calling `free()` on it when finished
 */
char* SkalBase64Encode(const uint8_t* data, int size_B);


/** Decode 4 base64 characters into (up to) 3 bytes
 *
 * The input string must be valid base64 characters using '=' as padding if and
 * when necessary. Blank characters are ignored, but at least 4 valid base64
 * characters must exist in the string.
 *
 * The input string must be null-terminated, although the null terminating
 * character does not have to be within the first 4 characters.
 *
 * @param pBase64 [in,out] Pointer to the base64 string to decode; must not be
 *                         NULL; must point to a valid base64 string
 * @param data    [out]    Decoded bytes; must not be NULL
 * @param size_B  [in]     Size of the above buffer, in bytes; must be >=3
 *
 * @return The number of decoded bytes (1, 2 or 3), or -1 if the input string is
 *         not a valid base64 string
 */
int SkalBase64Decode3(const char** pBase64, uint8_t* data, int size_B);


/** Decode a base64 string into bytes
 *
 * The input string must be null-terminated and must be valid base64 characters
 * using '=' as padding if and when necessary. Blank characters are ignored.
 *
 * @param base64 [in]  Base64 text to decode; must not be NULL
 * @param size_B [out] Number of decoded bytes; must not be NULL
 *
 * @return The decoded bytes, or NULL if `base64` is not a valid base64 string
 *         (in which case `*size_B` won't be touched); you must call `free()` on
 *         the it when finished.
 */
uint8_t* SkalBase64Decode(const char* base64, int* size_B);



/* @} */
#endif /* SKAL_COMMON_h_ */
