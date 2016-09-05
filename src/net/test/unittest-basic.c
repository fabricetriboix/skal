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

#include "skal-net.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool skalNetTestGroupEntry(void)
{
    SkalPlfInit();
    return RTTrue;
}

static RTBool skalNetTestGroupExit(void)
{
    SkalPlfExit();
    return RTTrue;
}

RTT_GROUP_START(TestNetBasic, 0x000110001u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

static SkalNet* gNet = NULL;

RTT_TEST_START(skal_net_basic_should_parse_ip4_string)
{
    uint32_t ip4;
    RTT_EXPECT(SkalNetStringToIp4("1.2.3.4", &ip4));
    RTT_EXPECT(0x01020304 == ip4);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_print_ip4)
{
    char buffer[32];
    SkalNetIp4ToString(0xDEADBEEF, buffer, sizeof(buffer));
    RTT_EXPECT(strcmp(buffer, "222.173.190.239") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_create_set)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_destroy_set)
{
    SkalNetDestroy(gNet);
    gNet = NULL;
}
RTT_TEST_END

RTT_GROUP_END(TestNetBasic,
        skal_net_basic_should_parse_ip4_string,
        skal_net_basic_should_print_ip4,
        skal_net_basic_should_create_set,
        skal_net_basic_should_destroy_set)
