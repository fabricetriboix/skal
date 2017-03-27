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

#include "skal-queue.h"
#include "skal-msg.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool testMsgGroupEnter(void)
{
    SkalPlfInit();
    SkalPlfThreadMakeSkal_DEBUG("TestQueue");
    return RTTrue;
}

static RTBool testMsgGroupExit(void)
{
    SkalPlfThreadUnmakeSkal_DEBUG();
    SkalPlfExit();
    return RTTrue;
}


static SkalQueue* gQueue = NULL;

RTT_GROUP_START(TestSkalQueue, 0x00040002u, testMsgGroupEnter, testMsgGroupExit)

RTT_TEST_START(skal_should_create_queue)
{
    gQueue = SkalQueueCreate("testq", 2);
    RTT_ASSERT(gQueue != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_should_push_a_msg)
{
    SkalMsg* msg = SkalMsgCreate("TestMsg", "dst1", 0, NULL);
    RTT_ASSERT(msg != NULL);
    SkalQueuePush(gQueue, msg);
    RTT_EXPECT(!SkalQueueIsOverHighThreshold(gQueue));
}
RTT_TEST_END

RTT_TEST_START(skal_should_push_an_urgent_msg_and_signal_full)
{
    SkalMsg* msg = SkalMsgCreate("UrgentMsg", "dst2",
            SKAL_MSG_FLAG_URGENT, NULL);
    RTT_ASSERT(msg != NULL);
    SkalQueuePush(gQueue, msg);
    RTT_EXPECT(SkalQueueIsOverHighThreshold(gQueue));
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_urgent_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue, false);
    RTT_ASSERT(msg != NULL);
    const char* name = SkalMsgName(msg);
    RTT_ASSERT(name != NULL);
    RTT_EXPECT(strcmp(name, "UrgentMsg") == 0);
    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_regular_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue, false);
    RTT_ASSERT(msg != NULL);
    const char* name = SkalMsgName(msg);
    RTT_ASSERT(name != NULL);
    RTT_EXPECT(strcmp(name, "TestMsg") == 0);
    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_destroy_queue)
{
    SkalQueueDestroy(gQueue);
}
RTT_TEST_END

RTT_TEST_START(skal_should_have_no_more_msg_ref_2)
{
    RTT_EXPECT(SkalMsgRefCount_DEBUG() == 0);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalQueue,
        skal_should_create_queue,
        skal_should_push_a_msg,
        skal_should_push_an_urgent_msg_and_signal_full,
        skal_should_pop_urgent_msg,
        skal_should_pop_regular_msg,
        skal_should_destroy_queue,
        skal_should_have_no_more_msg_ref_2)
