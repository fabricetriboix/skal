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

#include "skal-msg.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static SkalMsg* gMsg = NULL;

RTT_GROUP_START(TestSkalMsg, 0x00040001u, NULL, NULL)

RTT_TEST_START(skal_should_create_msg)
{
    gMsg = SkalMsgCreate("TestType", 0, NULL);
    RTT_ASSERT(gMsg != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_type)
{
    const char* type = SkalMsgType(gMsg);
    RTT_ASSERT(type != NULL);
    RTT_EXPECT(strcmp(type, "TestType") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_marker)
{
    const char* marker = SkalMsgMarker(gMsg);
    RTT_ASSERT(marker != NULL);
    RTT_ASSERT(strlen(marker) > 0);
    RTT_ASSERT(strlen(marker) < SKAL_NAME_MAX);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_add_int)
{
    SkalMsgAddInt(gMsg, "TestInt", -789);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_add_double)
{
    SkalMsgAddDouble(gMsg, "TestDouble", 345.678);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_add_string)
{
    SkalMsgAddString(gMsg, "TestString", "This is a test string");
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_int)
{
    RTT_EXPECT(SkalMsgGetInt(gMsg, "TestInt") == -789);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_double)
{
    double d = SkalMsgGetDouble(gMsg, "TestDouble");
    double diff = d - 345.678;
    if (diff < 0) {
        diff = -diff;
    }
    RTT_EXPECT(diff < 0.00001);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_string)
{
    const char* s = SkalMsgGetString(gMsg, "TestString");
    RTT_EXPECT(strcmp(s, "This is a test string") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_should_free_msg)
{
    SkalMsgUnref(gMsg);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalMsg,
        skal_should_create_msg,
        skal_msg_should_have_correct_type,
        skal_msg_should_have_marker,
        skal_msg_add_int,
        skal_msg_add_double,
        skal_msg_add_string,
        skal_msg_should_have_correct_int,
        skal_msg_should_have_correct_double,
        skal_msg_should_have_correct_string,
        skal_should_free_msg)


static SkalQueue* gQueue = NULL;

RTT_GROUP_START(TestSkalQueue, 0x00040002u, NULL, NULL)

RTT_TEST_START(skal_should_create_queue)
{
    gQueue = SkalQueueCreate(2);
    RTT_ASSERT(gQueue != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_should_push_a_msg)
{
    SkalMsg* msg = SkalMsgCreate("TestMsg", 0, NULL);
    RTT_ASSERT(msg != NULL);
    RTT_EXPECT(SkalQueuePush(gQueue, msg) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_should_push_an_urgent_msg_and_signal_full)
{
    SkalMsg* msg = SkalMsgCreate("UrgentMsg", SKAL_MSG_FLAG_URGENT, NULL);
    RTT_ASSERT(msg != NULL);
    RTT_EXPECT(SkalQueuePush(gQueue, msg) == 1);
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_urgent_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue);
    RTT_ASSERT(msg != NULL);
    const char* type = SkalMsgType(msg);
    RTT_ASSERT(type != NULL);
    RTT_EXPECT(strcmp(type, "UrgentMsg") == 0);
    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_shutdown_queue)
{
    SkalQueueShutdown(gQueue);
}
RTT_TEST_END

RTT_TEST_START(skal_should_not_push_msg)
{
    SkalMsg* msg = SkalMsgCreate("Bla", 0, NULL);
    RTT_ASSERT(msg != NULL);
    RTT_EXPECT(SkalQueuePush(gQueue, msg) == -1);
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_regular_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue);
    RTT_ASSERT(msg != NULL);
    const char* type = SkalMsgType(msg);
    RTT_ASSERT(type != NULL);
    RTT_EXPECT(strcmp(type, "TestMsg") == 0);
    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_destroy_queue)
{
    SkalQueueDestroy(gQueue);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalQueue,
        skal_should_create_queue,
        skal_should_push_a_msg,
        skal_should_push_an_urgent_msg_and_signal_full,
        skal_should_pop_urgent_msg,
        skal_should_shutdown_queue,
        skal_should_not_push_msg,
        skal_should_pop_regular_msg,
        skal_should_destroy_queue)
