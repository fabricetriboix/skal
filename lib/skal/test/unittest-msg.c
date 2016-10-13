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
#include "skal-alarm.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool testMsgGroupEnter(void)
{
    SkalPlfInit();
    return RTTrue;
}

static RTBool testMsgGroupExit(void)
{
    SkalPlfExit();
    return RTTrue;
}


static SkalMsg* gMsg = NULL;
static SkalAlarm* gAlarm1 = NULL;
static SkalAlarm* gAlarm2 = NULL;

RTT_GROUP_START(TestSkalMsg, 0x00040001u, testMsgGroupEnter, testMsgGroupExit)

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

// TODO: test attaching blob to msg

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

RTT_TEST_START(skal_msg_should_attach_alarms)
{
    gAlarm1 = SkalAlarmCreate("alarm1", SKAL_ALARM_NOTICE, true, true, NULL);
    RTT_ASSERT(gAlarm1 != NULL);
    SkalAlarmRef(gAlarm1);
    SkalMsgAttachAlarm(gMsg, gAlarm1);

    gAlarm2 = SkalAlarmCreate("alarm2", SKAL_ALARM_ERROR, false, false,
            "This is a %s", "test");
    RTT_ASSERT(gAlarm2 != NULL);
    SkalAlarmRef(gAlarm2);
    SkalMsgAttachAlarm(gMsg, gAlarm2);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_produce_correct_json)
{
    char* json = SkalMsgToJson(gMsg);
    RTT_ASSERT(json != NULL);

    // NB: Fields will be ordered by name
    char* expected = SkalSPrintf(
        "{\n"
        " \"version\": 1,\n"
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
        " ],\n"
        " \"alarms\": [\n"
        "  {\n"
        "   \"type\": \"alarm1\",\n"
        "   \"severity\": \"notice\",\n"
        "   \"origin\": \"TestThread\",\n"
        "   \"isOn\": true,\n"
        "   \"autoOff\": true,\n"
        "   \"timestamp_us\": %lld,\n"
        "   \"comment\": \"\"\n"
        "  },\n"
        "  {\n"
        "   \"type\": \"alarm2\",\n"
        "   \"severity\": \"error\",\n"
        "   \"origin\": \"TestThread\",\n"
        "   \"isOn\": false,\n"
        "   \"autoOff\": false,\n"
        "   \"timestamp_us\": %lld,\n"
        "   \"comment\": \"This is a test\"\n"
        "  }\n"
        " ]\n"
        "}\n",
        (long long)SkalAlarmTimestamp_us(gAlarm1),
        (long long)SkalAlarmTimestamp_us(gAlarm2));

    RTT_EXPECT(strcmp(json, expected) == 0);
    free(json);
    free(expected);

    SkalAlarmUnref(gAlarm1);
    SkalAlarmUnref(gAlarm2);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_detach_alarm1)
{
    SkalAlarm* alarm = SkalMsgDetachAlarm(gMsg);
    RTT_EXPECT(alarm != NULL);
    const char* s = SkalAlarmType(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "alarm1") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_NOTICE);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "TestThread") == 0);
    RTT_EXPECT(SkalAlarmIsOn(alarm));
    RTT_EXPECT(SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmComment(alarm) == NULL);
    SkalAlarmUnref(alarm);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_detach_alarm2)
{
    SkalAlarm* alarm = SkalMsgDetachAlarm(gMsg);
    RTT_EXPECT(alarm != NULL);
    const char* s = SkalAlarmType(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "alarm2") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_ERROR);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "TestThread") == 0);
    RTT_EXPECT(!SkalAlarmIsOn(alarm));
    RTT_EXPECT(!SkalAlarmAutoOff(alarm));
    s = SkalAlarmComment(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "This is a test") == 0);
    SkalAlarmUnref(alarm);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_no_more_alarms)
{
    SkalAlarm* alarm = SkalMsgDetachAlarm(gMsg);
    RTT_EXPECT(NULL == alarm);
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

RTT_TEST_START(skal_msg_should_create_from_json)
{
    const char* json =
        "{\n"
        " \"type\": \"SomeType\",\n"
        " \"sender\": \"SomeThread\",\n"
        " \"reci\\pient\": \"you\",\n"
        " \"marker\": \"Some\\\"Marker\",\n"
        " \"flags\": 3,\n"
        " \"iflags\": 128,\n"
        " \"version\": 1,\n"
        " \"fields\": [\n"
        "  {\n"
        "   \"name\": \"SomeDouble\",\n"
        "   \"value\": 3.456779999999999972715e+02,\n"
        "   \"type\": \"double\",\n"
        "  },\n"
        "  {\n"
        "   \"value\": \"ESIzRA==\",\n"
        "   \"name\": \"SomeMiniblob\",\n"
        "   \"type\": \"miniblob\",\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"SomeInt\",\n"
        "   \"type\": \"int\",\n"
        "   \"value\": -1789\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"SomeString\",\n"
        "   \"type\": \"string\",\n"
        "   \"value\": \"This is a test string2\"\n"
        "  }\n"
        " ],\n"
        " \"alarms\": [\n"
        "  {\n"
        "   \"timestamp_us\": 1234567890,\n"
        "   \"severity\": \"notice\",\n"
        "   \"origin\": \"PanicAttak\",\n"
        "   \"isOn\": true,\n"
        "   \"type\": \"AlarmTypeA\",\n"
        "   \"autoOff\": false\n"
        "  },\n"
        "  {\n"
        "   \"timestamp_us\": 987654321,\n"
        "   \"severity\": \"warning\",\n"
        "   \"isOn\": false,\n"
        "   \"type\": \"AlarmTypeB\",\n"
        "   \"autoOff\": true,\n"
        "   \"comment\": \"This is a \\fake alarm\"\n"
        "  }\n"
        " ]\n"
        "}\n";

    SkalMsg* msg = SkalMsgCreateFromJson(json);
    RTT_EXPECT(msg != NULL);

    const char* s = SkalMsgType(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "SomeType") == 0);

    s = SkalMsgSender(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "SomeThread") == 0);

    s = SkalMsgRecipient(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "you") == 0);

    uint8_t i = SkalMsgFlags(msg);
    RTT_EXPECT(SKAL_MSG_FLAG_UDP == i);

    i = SkalMsgIFlags(msg);
    RTT_EXPECT(SKAL_MSG_IFLAG_INTERNAL == i);

    s = SkalMsgMarker(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "Some\"Marker") == 0);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeDouble"));
    double d = SkalMsgGetDouble(gMsg, "SomeDouble");
    double diff = d - 345.678;
    if (diff < 0) {
        diff = -diff;
    }
    RTT_EXPECT(diff < 0.00001);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeMiniblob"));
    int size_B;
    const uint8_t* data = SkalMsgGetMiniblob(gMsg, "SomeMiniblob", &size_B);
    RTT_EXPECT(data != NULL);
    RTT_EXPECT(4 == size_B);
    uint8_t expected[4] = { 0x11, 0x22, 0x33, 0x44 };
    RTT_EXPECT(memcmp(data, expected, sizeof(expected)) == 0);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeInt"));
    RTT_EXPECT(SkalMsgGetInt(msg, "SomeInt") == -1789);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeString"));
    s = SkalMsgGetString(msg, "SomeString");
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "This is a test string2") == 0);

    SkalAlarm* alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(alarm != NULL);
    s = SkalAlarmType(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "AlarmTypeA") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_NOTICE);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "PanicAttak") == 0);
    RTT_EXPECT(SkalAlarmIsOn(alarm));
    RTT_EXPECT(!SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmTimestamp_us(alarm) == 1234567890LL);
    RTT_EXPECT(SkalAlarmComment(alarm) == NULL);
    SkalAlarmUnref(alarm);

    alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(alarm != NULL);
    s = SkalAlarmType(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "AlarmTypeB") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_WARNING);
    RTT_EXPECT(SkalAlarmOrigin(alarm) == NULL);
    RTT_EXPECT(!SkalAlarmIsOn(alarm));
    RTT_EXPECT(SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmTimestamp_us(alarm) == 987654321LL);
    s = SkalAlarmComment(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "This is a fake alarm") == 0);
    SkalAlarmUnref(alarm);

    alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(NULL == alarm);

    SkalMsgUnref(msg);
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
        skal_msg_should_attach_alarms,
        skal_msg_should_produce_correct_json,
        skal_msg_should_detach_alarm1,
        skal_msg_should_detach_alarm2,
        skal_msg_should_have_no_more_alarms,
        skal_msg_should_free,
        skal_should_have_no_more_msg_ref_1,
        skal_msg_should_create_from_json)
