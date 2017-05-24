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

#ifndef SKAL_COMMON_h_
#define SKAL_COMMON_h_

#ifdef __cplusplus
extern "C" {
#endif


/** Common stuff for SKAL
 *
 * @defgroup skalcommon Common stuff for SKAL
 * @addtogroup skalcommon
 * @{
 */

#include "skal-plf.h"
#include <stdarg.h>



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
#define SkalMalloc(size_B) _SkalMalloc((size_B), __FILE__, __LINE__)

/** @cond hidden */
void* _SkalMalloc(int size_B, const char* file, int line);
/** @endcond */


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
#define SkalMallocZ(size_B) _SkalMallocZ((size_B), __FILE__, __LINE__)

/** @cond hidden */
void* _SkalMallocZ(int size_B, const char* file, int line);
/** @endcond */


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
#define SkalRealloc(ptr, size_B) \
    _SkalRealloc((ptr), (size_B), __FILE__, __LINE__)

/** @cond hidden */
void* _SkalRealloc(void* ptr, int size_B, const char* file, int line);
/** @endcond */


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
#define SkalCalloc(nItems, itemSize_B) \
    _SkalCalloc((nItems), (itemSize_B), __FILE__, __LINE__)

/** @cond hidden */
void* _SkalCalloc(int nItems, int itemSize_B, const char* file, int line);
/** @endcond */


/** `strdup()` replacement
 *
 * The behaviour of this function is the same as for `strdup(3)`, but it asserts
 * if it fails.
 *
 * @param s [in] String to copy; may be NULL; if not NULL, must be
 *               null-terminated
 *
 * @return A pointer to the newly allocated string, or NULL if `s` is NULL;
 *         please release with `free()` when you no longer need it
 */
#define SkalStrdup(s) _SkalStrdup((s), __FILE__, __LINE__)

/** @cond hidden */
char* _SkalStrdup(const char* s, const char* file, int line);
/** @endcond */


/** Helper function to sprintf a string
 *
 * @param _format [in] A printf-like format string
 * @param ...     [in] Printf-like arguments
 *
 * @return The formatted string, never NULL; please release with `free()` when
 *         you no longer need it
 */
#define SkalSPrintf(_format, ...) \
        _SkalSPrintf(__FILE__, __LINE__, (_format), ## __VA_ARGS__)

/** @cond hidden */
char* _SkalSPrintf(const char* file, int line, const char* format, ...)
    __attribute__(( format(printf, 3, 4) ));
/** @endcond */


/** Helper function to vsprintf a string
 *
 * @param _format [in]     A printf-like format string
 * @param _ap     [in,out] stdarg arguments
 *
 * @return The formatted string, never NULL; please release with `free()` when
 *         you no longer need it
 */
#define SkalVSPrintf(_format, _ap) \
        _SkalVSPrintf(__FILE__, __LINE__, (_format), (_ap))

/** @cond hidden */
char* _SkalVSPrintf(const char* file, int line, const char* format, va_list ap);
/** @endcond */


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


/** Check the given string is pure ASCII
 *
 * @param str [in] String to check; must not be NULL; must be null-terminated
 *
 * @return `true` if the string contains only printable ASCII characters;
 *         `false` if is contains one or more non-ASCII characters or
 *         non-printable characters.
 */
bool SkalIsAsciiString(const char* str);


/** Safer replacement for `strcmp(3)`
 *
 * @param lhs [in] Left-hand side string to compare; must be NULL or a UTF-8
 *                 string; may be NULL
 * @param rhs [in] Right-hand side string to compare; must be NULL or a UTF-8
 *                 string; may be NULL
 *
 * @return A strictly negative number if lhs < rhs, 0 if lhs == rhs, a strictly
 *         positive number if lhs > rhs
 */
int SkalStrcmp(const char* lhs, const char* rhs);


/** Safer replacement for `strncmp(3)`
 *
 * @param lhs [in] Left-hand side string to compare; must be NULL or a UTF-8
 *                 string; may be NULL
 * @param rhs [in] Right-hand side string to compare; must be NULL or a UTF-8
 *                 string; may be NULL
 * @param n   [in] Maximum number of bytes to compare
 *
 * @return A strictly negative number if lhs < rhs, 0 if lhs == rhs, a strictly
 *         positive number if lhs > rhs
 */
int SkalStrncmp(const char* lhs, const char* rhs, size_t n);


/** Check if the given string starts with a certain string
 *
 * @param str     [in] String to check; may be NULL
 * @param pattern [in] Pattern at the beginning; may be NULL
 *
 * @return `true` if `str` starts with `pattern`, `false` otherwise
 */
bool SkalStartsWith(const char* str, const char* pattern);


/** Standard string comparison function suitable for CdsMap
 *
 * @param leftkey  [in] LHS token; must not be NULL, must be a null-terminated
 *                      UTF-8 string
 * @param rightkey [in] RHS token; must not be NULL, must be a null-terminated
 *                      UTF-8 string
 * @param cookie   [in] Unused
 *
 * @return -1 if `leftkey` < `rightkey`, 0 if `leftkey` == `rightkey`,
 *         +1 if `leftkey` > `rightkey`
 */
int SkalStringCompare(void* leftkey, void* rightkey, void* cookie);


/** Standard binary comparison function suitable for CdsMap
 *
 * @param leftKey  [in] LHS token; must not be NULL
 * @param rightKey [in] RHS token; must not be NULL
 * @param cookie   [in] Actually a `size_t` value, which is the number of bytes
 *                      to compare; must be >0
 *
 * @return -1 if `leftkey` < `rightkey`, 0 if `leftkey` == `rightkey`,
 *         +1 if `leftkey` > `rightkey`
 */
int SkalMemCompare(void* leftKey, void* rightKey, void* cookie);


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


/** Log an error string
 *
 * The use of this function is highly discouraged. Alarms should be used
 * whenever possible, including signalling of internal errors. In short, this
 * function is to be used only for very low level errors that need to be somehow
 * logged or reported, but alarms are not available for one reason or another.
 */
#define SkalLog(_format, ...) \
    _SkalLog(__FILE__, __LINE__, (_format), ## __VA_ARGS__)

/** @cond hidden */
void _SkalLog(const char* file, int line, const char* format, ...)
    __attribute__(( format(printf, 3, 4) ));
/** @endcond */


/** Enable logging through `SkalLog()`
 *
 * Logging is enabled by default.
 *
 * @param enable [in] Whether to enable or disable logging
 */
void SkalLogEnable(bool enable);



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKAL_COMMON_h_ */
