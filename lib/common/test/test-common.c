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

#include "skal-common.h"
#include "rttest.h"

#include <string.h>


static RTBool testCommonGroupEnter(void)
{
    SkalPlfInit();
    return RTTrue;
}

static RTBool testCommonGroupExit(void)
{
    SkalPlfExit();
    return RTTrue;
}


RTT_GROUP_START(TestSPrintf, 0x00020001u,
        testCommonGroupEnter, testCommonGroupExit)

static const char* gLongString =
    "Copyright (c) 2016  Fabrice Triboix\n"
    "\n"
    "This program is free software: you can redistribute it and/or modify\n"
    "it under the terms of the GNU General Public License as published by\n"
    "the Free Software Foundation, either version 3 of the License, or\n"
    "(at your option) any later version.\n"
    "\n"
    "This program is distributed in the hope that it will be useful,\n"
    "but WITHOUT ANY WARRANTY; without even the implied warranty of\n"
    "MERCHANTABILITY or FITNESS FOR A PARTICULAR PURPOSE.  See the\n"
    "GNU General Public License for more details.\n"
    "\n"
    "You should have received a copy of the GNU General Public License\n"
    "along with this program.  If not, see <http://www.gnu.org/licenses/>.\n";

static SkalStringBuilder* gSB = NULL;


RTT_TEST_START(skal_sprintf_should_format_a_string)
{
    const char* world = "world";
    int x = 19;
    char* s = SkalSPrintf("Hello %s! %d", world, x);
    RTT_ASSERT(s != NULL);
    RTT_ASSERT(strcmp(s, "Hello world! 19") == 0);
    free(s);
}
RTT_TEST_END

RTT_TEST_START(skal_sprintf_should_format_a_long_string)
{
    char* s = SkalSPrintf("%s", gLongString);
    RTT_ASSERT(s != NULL);
    RTT_ASSERT(strcmp(s, gLongString) == 0);
    free(s);
}
RTT_TEST_END

RTT_TEST_START(skal_sb_should_create_string_builder)
{
    gSB = SkalStringBuilderCreate(1);
    RTT_ASSERT(gSB != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_sb_should_append_stuff)
{
    SkalStringBuilderAppend(gSB, "%d", 18);
    SkalStringBuilderAppend(gSB, "%s", "Hello world!");
    SkalStringBuilderAppend(gSB, "%c", '?');
    SkalStringBuilderAppend(gSB, "%c", '?');
    SkalStringBuilderAppend(gSB, "%s", "XYZ");
}
RTT_TEST_END

RTT_TEST_START(skal_sb_should_trim)
{
    SkalStringBuilderTrim(gSB, 4);
}
RTT_TEST_END

RTT_TEST_START(skal_sb_should_finish)
{
    char* str = SkalStringBuilderFinish(gSB);
    gSB = NULL;
    RTT_EXPECT(strcmp(str, "18Hello world!?") == 0);
    free(str);
}
RTT_TEST_END

RTT_GROUP_END(TestSPrintf,
        skal_sprintf_should_format_a_string,
        skal_sprintf_should_format_a_long_string,
        skal_sb_should_create_string_builder,
        skal_sb_should_append_stuff,
        skal_sb_should_trim,
        skal_sb_should_finish)

RTT_GROUP_START(TestBase64, 0x00020002u,
        testCommonGroupEnter, testCommonGroupExit)

RTT_TEST_START(skal_base64_should_encode3_1_byte)
{
    uint8_t bytes[1] = { 0 };
    char buffer[5];
    memset(buffer, 0, sizeof(buffer));
    int n = SkalBase64Encode3(bytes, sizeof(bytes), buffer, sizeof(buffer));
    RTT_EXPECT(1 == n);
    RTT_EXPECT(strcmp(buffer, "AA==") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode3_2_bytes)
{
    uint8_t bytes[2] = { 0xca, 0xfe };
    char buffer[5];
    memset(buffer, 0, sizeof(buffer));
    int n = SkalBase64Encode3(bytes, sizeof(bytes), buffer, sizeof(buffer));
    RTT_EXPECT(2 == n);
    RTT_EXPECT(strcmp(buffer, "yv4=") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode3_3_bytes)
{
    uint8_t bytes[3] = { 0xca, 0xfe, 0xaa };
    char buffer[5];
    memset(buffer, 0, sizeof(buffer));
    int n = SkalBase64Encode3(bytes, sizeof(bytes), buffer, sizeof(buffer));
    RTT_EXPECT(3 == n);
    RTT_EXPECT(strcmp(buffer, "yv6q") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode_1_byte)
{
    uint8_t bytes[1] = { 0xff };
    char* str = SkalBase64Encode(bytes, sizeof(bytes));
    RTT_ASSERT(str != NULL);
    RTT_EXPECT(strcmp(str, "/w==") == 0);
    free(str);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode_2_bytes)
{
    uint8_t bytes[2] = { 0xff, 0x00 };
    char* str = SkalBase64Encode(bytes, sizeof(bytes));
    RTT_ASSERT(str != NULL);
    RTT_EXPECT(strcmp(str, "/wA=") == 0);
    free(str);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode_3_bytes)
{
    uint8_t bytes[3] = { 0xff, 0x00, 0x55 };
    char* str = SkalBase64Encode(bytes, sizeof(bytes));
    RTT_ASSERT(str != NULL);
    RTT_EXPECT(strcmp(str, "/wBV") == 0);
    free(str);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode_4_bytes)
{
    uint8_t bytes[4] = { 0xff, 0x00, 0x55, 0x11 };
    char* str = SkalBase64Encode(bytes, sizeof(bytes));
    RTT_ASSERT(str != NULL);
    RTT_EXPECT(strcmp(str, "/wBVEQ==") == 0);
    free(str);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_encode_10_bytes)
{
    uint8_t bytes[10] = { 0x0f, 0xa3, 0xf0, 0x72, 0x00, 0xd5, 0x54, 0x11, 0x87,
        0xad };
    char* str = SkalBase64Encode(bytes, sizeof(bytes));
    RTT_ASSERT(str != NULL);
    RTT_EXPECT(strcmp(str, "D6PwcgDVVBGHrQ==") == 0);
    free(str);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode3_1_byte)
{
    const char* str = "AA==";
    uint8_t data[3];
    int n = SkalBase64Decode3(&str, data, sizeof(data));
    RTT_EXPECT(1 == n);
    uint8_t expected[1] = { 0 };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode3_2_bytes)
{
    const char* str = "yv4=";
    uint8_t data[3];
    int n = SkalBase64Decode3(&str, data, sizeof(data));
    RTT_EXPECT(2 == n);
    uint8_t expected[2] = { 0xca, 0xfe };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode3_3_bytes)
{
    const char* str = "yv6q";
    uint8_t data[3];
    int n = SkalBase64Decode3(&str, data, sizeof(data));
    RTT_EXPECT(3 == n);
    uint8_t expected[3] = { 0xca, 0xfe, 0xaa };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode_1_byte)
{
    const char* str = "/w==";
    int size_B;
    uint8_t* data = SkalBase64Decode(str, &size_B);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(1 == size_B);
    uint8_t expected[1] = { 0xff };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode_2_bytes)
{
    const char* str = "/wA=";
    int size_B;
    uint8_t* data = SkalBase64Decode(str, &size_B);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(2 == size_B);
    uint8_t expected[2] = { 0xff, 0x00 };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode_3_bytes)
{
    const char* str = "/wBV";
    int size_B;
    uint8_t* data = SkalBase64Decode(str, &size_B);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(3 == size_B);
    uint8_t expected[3] = { 0xff, 0x00, 0x55 };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode_4_bytes)
{
    const char* str = "/wBVEQ==";
    int size_B;
    uint8_t* data = SkalBase64Decode(str, &size_B);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(4 == size_B);
    uint8_t expected[4] = { 0xff, 0x00, 0x55, 0x11 };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
}
RTT_TEST_END

RTT_TEST_START(skal_base64_should_decode_10_bytes)
{
    const char* str = "D6PwcgDVVBGHrQ==";
    int size_B;
    uint8_t* data = SkalBase64Decode(str, &size_B);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(10 == size_B);
    uint8_t expected[10] = { 0x0f, 0xa3, 0xf0, 0x72, 0x00, 0xd5, 0x54, 0x11,
        0x87, 0xad };
    RTT_ASSERT(memcmp(data, expected, sizeof(expected)) == 0);
    free(data);
}
RTT_TEST_END

RTT_GROUP_END(TestBase64,
        skal_base64_should_encode3_1_byte,
        skal_base64_should_encode3_2_bytes,
        skal_base64_should_encode3_3_bytes,
        skal_base64_should_encode_1_byte,
        skal_base64_should_encode_2_bytes,
        skal_base64_should_encode_3_bytes,
        skal_base64_should_encode_4_bytes,
        skal_base64_should_encode_10_bytes,
        skal_base64_should_decode3_1_byte,
        skal_base64_should_decode3_2_bytes,
        skal_base64_should_decode3_3_bytes,
        skal_base64_should_decode_1_byte,
        skal_base64_should_decode_2_bytes,
        skal_base64_should_decode_3_bytes,
        skal_base64_should_decode_4_bytes,
        skal_base64_should_decode_10_bytes)
