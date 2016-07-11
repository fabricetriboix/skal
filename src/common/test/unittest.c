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
#include "rttest.h"

#include <string.h>


RTT_GROUP_START(TestSPrintf, 0x00020001u, NULL, NULL)

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

RTT_GROUP_END(TestSPrintf,
        skal_sprintf_should_format_a_string)
