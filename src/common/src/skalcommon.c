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

#include "skalcommon.h"
#include <stdarg.h>
#include <string.h>



/*----------------+
 | Types & Macros |
 +----------------*/


/** Initial size of a dynamic string, in characters */
#define SKAL_INITIAL_STRING_LENGTH 256



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void* SkalMalloc(int size_B)
{
    SKALASSERT(size_B > 0);
    void* ptr = malloc(size_B);
    SKALASSERT(ptr != NULL);
    return ptr;
}


void* SkalMallocZ(int size_B)
{
    SKALASSERT(size_B > 0);
    void* ptr = malloc(size_B);
    SKALASSERT(ptr != NULL);
    memset(ptr, 0, size_B);
    return ptr;
}


void* SkalRealloc(void* ptr, int size_B)
{
    SKALASSERT(size_B > 0);
    void* newptr = realloc(ptr, size_B);
    if (newptr == NULL) {
        free(ptr);
    }
    SKALASSERT(newptr != NULL);
    return newptr;
}


void* SkalCalloc(int nItems, int itemSize_B)
{
    SKALASSERT(nItems > 0);
    SKALASSERT(itemSize_B > 0);
    void* ptr = calloc(nItems, itemSize_B);
    SKALASSERT(ptr != NULL);
    return ptr;
}


char* SkalSPrintf(const char* format, ...)
{
    va_list ap;
    va_start(ap, format);

    // NB: `vsnprintf()` will modify `ap`, so we need to make a copy beforehand
    // if we want to call `vsnprintf()` a 2nd time.
    va_list ap2;
    va_copy(ap2, ap);

    int len = SKAL_INITIAL_STRING_LENGTH;
    char* str = SkalMalloc(len);
    int n = vsnprintf(str, len, format, ap);
    if (n >= len) {
        len = n + 1;
        str = SkalRealloc(str, len);
        n = vsnprintf(str, len, format, ap2);
        SKALASSERT(n <= len);
    }

    va_end(ap2);
    va_end(ap);
    return str;
}


bool SkalIsAsciiString(const char* str, int maxlen)
{
    SKALASSERT(str != NULL);
    SKALASSERT(maxlen > 0);

    for (int i = 0; i < maxlen; i++) {
        char c = str[i];
        if ('\0' == c) {
            return true; // Null char found => `str` is a valid ASCII string
        }
        if ((c < 0x20) || (0x7f == c)) {
            // Control character found => `str` is not a valid ASCII string
            return false;
        }
        if (c & 0x80) {
            // Extended ASCII char found => `str` is not a valid ASCII string
            return false;
        }
    }

    // No null character found within `maxlen` bytes
    //  => `str` is not a valid ASCII string
    return false;
}


bool SkalIsUtf8String(const char* str, int maxlen)
{
    SKALASSERT(str != NULL);
    SKALASSERT(maxlen > 0);

    for (int i = 0; i < maxlen; i++) {
        char c = str[i];
        if ('\0' == c) {
            return true; // Null char found => `str` is a valid UTF-8 string
        }
        if ((c < 0x20) || (0x7f == c)) {
            // Control character found => `str` is not a valid UTF-8 string
            return false;
        }
    }

    // No null character found within `maxlen` bytes
    //  => `str` is not a valid UTF-8 string
    return false;
}
