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
#define SKAL_INITIAL_STRING_CAPACITY 256


struct SkalStringBuilder
{
    char* str;
    int capacity;
    int index;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Convert a 6-bit byte into a base64 character */
static char base64ByteToChar(uint8_t byte);


/** Get the next valid base64 character
 *
 * This function tries to find the next valid base64 character,
 * i.e. [A-Za-z0-9+/=]. If it does find such a character, it will return it and
 * move `*pBase64` to the character right after it. If it does not find a valid
 * base64 character, it will return '\0' and `*pBase64` will point to the
 * terminating null character.
 *
 * This function ignores blanks.
 *
 * \param pBase64 [in,out] Pointer to string to parse
 *
 * \return The found base64 character, or '\0' if not found
 */
static char nextValidBase64Char(const char** pBase64);


/** Convert a base64 char into a 6-bit byte
 *
 * \param c [in] Character to convert; must be a valid base64 character
 *
 * \return The decoded 6-bit byte
 */
static uint8_t base64CharToByte(char c);



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

    int capacity = SKAL_INITIAL_STRING_CAPACITY;
    char* str = SkalMalloc(capacity);
    int n = vsnprintf(str, capacity, format, ap);
    if (n >= capacity) {
        capacity = n + 1;
        str = SkalRealloc(str, capacity);
        n = vsnprintf(str, capacity, format, ap2);
        SKALASSERT(n < capacity);
    }

    va_end(ap2);
    va_end(ap);
    return str;
}


SkalStringBuilder* SkalStringBuilderCreate(int initialCapacity)
{
    SkalStringBuilder* sb = SkalMalloc(sizeof(*sb));
    if (initialCapacity > 0) {
        sb->capacity = initialCapacity;
    } else {
        sb->capacity = SKAL_INITIAL_STRING_CAPACITY;
    }
    sb->str = SkalMalloc(sb->capacity);
    sb->str[0] = '\0';
    sb->index = 0;
    return sb;
}


void SkalStringBuilderAppend(SkalStringBuilder* sb, const char* format, ...)
{
    SKALASSERT(sb != NULL);
    SKALASSERT(format != NULL);

    va_list ap;
    va_start(ap, format);

    // NB: `vsnprintf()` will modify `ap`, so we need to make a copy beforehand
    // if we want to call `vsnprintf()` a 2nd time.
    va_list ap2;
    va_copy(ap2, ap);

    int remaining = sb->capacity - sb->index;
    int n = vsnprintf(&(sb->str[sb->index]), remaining, format, ap);
    if (n >= remaining) {
        int missing = remaining - n;
        int extra = (missing * 2) + SKAL_INITIAL_STRING_CAPACITY;
        sb->capacity += extra;
        sb->str = SkalRealloc(sb->str, sb->capacity);

        remaining = sb->capacity - sb->index;
        n = vsnprintf(&(sb->str[sb->index]), remaining, format, ap2);
        SKALASSERT(n < remaining);
    }
    sb->index += n;

    va_end(ap2);
    va_end(ap);
}


void SkalStringBuilderTrim(SkalStringBuilder* sb, int n)
{
    SKALASSERT(sb != NULL);
    if (n > 0) {
        if (n > sb->index) {
            sb->index = 0;
        } else {
            sb->index -= n;
        }
        sb->str[sb->index] = '\0';
    }
}


char* SkalStringBuilderFinish(SkalStringBuilder* sb)
{
    SKALASSERT(sb != NULL);
    char* str = sb->str;
    free(sb);
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
        // TODO: there is a lot more to check for a UTF-8 string...
    }

    // No null character found within `maxlen` bytes
    //  => `str` is not a valid UTF-8 string
    return false;
}


int SkalStringCompare(void* leftkey, void* rightkey, void* cookie)
{
    return strncmp((const char*)leftkey, (const char*)rightkey, SKAL_NAME_MAX);
}


int SkalBase64Encode3(const uint8_t* data, int size_B,
        char* base64, int base64Size)
{
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);
    SKALASSERT(base64 != NULL);
    SKALASSERT(base64Size >= 4);

    uint8_t byte0 = data[0];
    uint8_t tmp = byte0 >> 2;
    base64[0] = base64ByteToChar(tmp);
    tmp = (byte0 << 4) & 0x30;

    if (1 == size_B) {
        base64[1] = base64ByteToChar(tmp);
        base64[2] = '=';
        base64[3] = '=';

    } else {
        uint8_t byte1 = data[1];
        tmp |= (byte1 >> 4) & 0x0f;
        base64[1] = base64ByteToChar(tmp);
        tmp = (byte1 << 2) & 0x3c;

        if (2 == size_B) {
            base64[2] = base64ByteToChar(tmp);
            base64[3] = '=';

        } else {
            uint8_t byte2 = data[2];
            tmp |= byte2 >> 6;
            base64[2] = base64ByteToChar(tmp);
            tmp = byte2 & 0x3f;
            base64[3] = base64ByteToChar(tmp);
        }
    }

    if (size_B > 3) {
        size_B = 3;
    }
    return size_B;
}


char* SkalBase64Encode(const uint8_t* data, int size_B)
{
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);

    // Size of output string: 4 characters for every 3 input bytes, rounded up,
    // plus terminating null character.
    int base64Size = (((size_B + 2) / 3) * 4) + 1;

    char* base64 = SkalMalloc(base64Size);
    char* ptr = base64;
    while (size_B > 0) {
        int n = SkalBase64Encode3(data, size_B, ptr, base64Size);
        data += n;
        size_B -= n;
        ptr += 4;
        base64Size -= 4;
    }
    ptr[0] = '\0';
    return base64;
}


int SkalBase64Decode3(const char** pBase64, uint8_t* data, int size_B)
{
    SKALASSERT(pBase64 != NULL);
    SKALASSERT(*pBase64 != NULL);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B >= 3);

    bool ok = false;
    char c0 = nextValidBase64Char(pBase64);
    char c1;
    char c2;
    char c3;
    if ((c0 != '\0') && (c0 != '=')) {
        c1 = nextValidBase64Char(pBase64);
        if ((c1 != '\0') && (c1 != '=')) {
            c2 = nextValidBase64Char(pBase64);
            if (c2 != '\0') {
                c3 = nextValidBase64Char(pBase64);
                ok = true;
            }
        }
    }

    int count = -1;
    if (ok) {
        count = 1;
        uint8_t tmp = base64CharToByte(c0);
        data[0] = tmp << 2;

        tmp = base64CharToByte(c1);
        data[0] |= tmp >> 4;

        if (c2 != '=') {
            count = 2;
            data[1] = tmp << 4;
            tmp = base64CharToByte(c2);
            data[1] |= tmp >> 2;

            if (c3 != '=') {
                count = 3;
                data[2] = tmp << 6;
                tmp = base64CharToByte(c3);
                data[2] |= tmp;
            }
        }
    }
    return count;
}


uint8_t* SkalBase64Decode(const char* base64, int* size_B)
{
    SKALASSERT(base64 != NULL);
    SKALASSERT(size_B != NULL);

    uint8_t* data = NULL;
    int len = strlen(base64);
    if (len >= 4) {
        // Size of output byte array: 3 bytes for every 4 input characters,
        // rounded up.
        int capacity = ((len + 3) / 4) * 3;
        data = SkalMalloc(capacity);
        uint8_t* ptr = data;
        bool isValid = true;
        int size = 0;
        while ((base64[0] != '\0') && isValid) {
            int n = SkalBase64Decode3(&base64, ptr, capacity);
            if (n < 0) {
                isValid = false;
            } else {
                ptr += n;
                capacity -= n;
                size += n;
            }
        }
        if (!isValid) {
            free(data);
            data = NULL;
        } else {
            *size_B = size;
        }
    }
    return data;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static char base64ByteToChar(uint8_t byte)
{
    char c;
    if (byte < 26) {
        c = 'A' + byte;

    } else if (byte < 52) {
        c = 'a' + byte - 26;

    } else if (byte < 62) {
        c = '0' + byte - 52;

    } else if (62 == byte) {
        c = '+';

    } else if (63 == byte) {
        c = '/';

    } else {
        SKALPANIC;
    }
    return c;
}


static inline bool isValidBase64Char(char c)
{
    return ((c >= 'A') && (c <= 'Z'))
        || ((c >= 'a') && (c <= 'z'))
        || ((c >= '0') && (c <= '9'))
        || ('+' == c) || ('/' == c) || ('=' == c);
}

static char nextValidBase64Char(const char** pBase64)
{
    char c = '\0';
    while ((**pBase64 != '\0') && ('\0' == c)) {
        if (isValidBase64Char(**pBase64)) {
            c = **pBase64;
        }
        (*pBase64)++;
    }
    return c;
}


static uint8_t base64CharToByte(char c)
{
    uint8_t byte;
    if ((c >= 'A') && (c <= 'Z')) {
        byte = c - 'A';

    } else if ((c >= 'a') && (c <= 'z')) {
        byte = c - 'a' + 26;

    } else if ((c >= '0') && (c <= '9')) {
        byte = c - '0' + 52;

    } else if ('+' == c) {
        byte = 62;

    } else if ('/' == c) {
        byte = 63;

    } else {
        SKALPANIC;
    }
    return byte;
}
