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
    SkalPlfThreadSetName("TestThread");

    // Fool skal-msg into thinking this thread is managed by SKAL
    SkalPlfThreadSetSpecific((void*)0xcafedeca);

    gMsg = SkalMsgCreate("TestType", "dummy-dst", 0, "TestMarker");
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

RTT_TEST_START(skal_msg_should_have_correct_sender)
{
    const char* sender = SkalMsgSender(gMsg);
    RTT_ASSERT(sender != NULL);
    RTT_EXPECT(strcmp(sender, "TestThread") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_recipient)
{
    const char* recipient = SkalMsgRecipient(gMsg);
    RTT_ASSERT(recipient != NULL);
    RTT_EXPECT(strcmp(recipient, "dummy-dst") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_marker)
{
    const char* marker = SkalMsgMarker(gMsg);
    RTT_ASSERT(marker != NULL);
    RTT_EXPECT(strcmp(marker, "TestMarker") == 0);
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

RTT_TEST_START(skal_msg_add_miniblob)
{
    uint8_t data[4] = { 0x11, 0x22, 0x33, 0x44 };
    SkalMsgAddMiniblob(gMsg, "TestMiniblob", data, sizeof(data));
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

RTT_TEST_START(skal_msg_should_have_correct_miniblob)
{
    int size_B;
    const uint8_t* data = SkalMsgGetMiniblob(gMsg, "TestMiniblob", &size_B);
    RTT_EXPECT(data != NULL);
    RTT_EXPECT(4 == size_B);
    uint8_t expected[4] = { 0x11, 0x22, 0x33, 0x44 };
    RTT_EXPECT(memcmp(data, expected, sizeof(expected)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_produce_correct_json)
{
    char* json = SkalMsgToJson(gMsg);
    RTT_ASSERT(json != NULL);

    // NB: Fields will be ordered by name
    const char* expected =
        "{\n"
        " \"type\": \"TestType\",\n"
        " \"sender\": \"TestThread\",\n"
        " \"recipient\": \"dummy-dst\",\n"
        " \"marker\": \"TestMarker\",\n"
        " \"flags\": 0,\n"
        " \"iflags\": 0,\n"
        " \"fields\": [\n"
        "  {\n"
        "   \"name\": \"TestDouble\",\n"
        "   \"type\": \"double\",\n"
        "   \"value\": 3.456779999999999972715e+02\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"TestInt\",\n"
        "   \"type\": \"int\",\n"
        "   \"value\": -789\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"TestMiniblob\",\n"
        "   \"type\": \"miniblob\",\n"
        "   \"value\": \"ESIzRA==\"\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"TestString\",\n"
        "   \"type\": \"string\",\n"
        "   \"value\": \"This is a test string\"\n"
        "  }\n"
        " ]\n"
        "}\n";

    RTT_EXPECT(strcmp(json, expected) == 0);
    free(json);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_free)
{
    SkalMsgUnref(gMsg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_have_no_more_msg_ref_1)
{
    RTT_EXPECT(SkalMsgRefCount_DEBUG() == 0);
    SkalPlfThreadSetSpecific(NULL); // reset thread-specific data
}
RTT_TEST_END

RTT_GROUP_END(TestSkalMsg,
        skal_should_create_msg,
        skal_msg_should_have_correct_type,
        skal_msg_should_have_correct_sender,
        skal_msg_should_have_correct_recipient,
        skal_msg_should_have_marker,
        skal_msg_add_int,
        skal_msg_add_double,
        skal_msg_add_string,
        skal_msg_add_miniblob,
        skal_msg_should_have_correct_int,
        skal_msg_should_have_correct_double,
        skal_msg_should_have_correct_string,
        skal_msg_should_have_correct_miniblob,
        skal_msg_should_produce_correct_json,
        skal_msg_should_free,
        skal_should_have_no_more_msg_ref_1)


static SkalQueue* gQueue = NULL;

RTT_GROUP_START(TestSkalQueue, 0x00040002u, NULL, NULL)

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
    RTT_EXPECT(!SkalQueueIsFull(gQueue));
}
RTT_TEST_END

RTT_TEST_START(skal_should_push_an_urgent_msg_and_signal_full)
{
    SkalMsg* msg = SkalMsgCreate("UrgentMsg", "dst2",
            SKAL_MSG_FLAG_URGENT, NULL);
    RTT_ASSERT(msg != NULL);
    SkalQueuePush(gQueue, msg);
    RTT_EXPECT(SkalQueueIsFull(gQueue));
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_urgent_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue, false);
    RTT_ASSERT(msg != NULL);
    const char* type = SkalMsgType(msg);
    RTT_ASSERT(type != NULL);
    RTT_EXPECT(strcmp(type, "UrgentMsg") == 0);
    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_should_pop_regular_msg)
{
    SkalMsg* msg = SkalQueuePop_BLOCKING(gQueue, false);
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
