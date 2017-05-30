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

#include "skal-msg.h"
#include "skal-blob.h"
#include "skal-alarm.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool testMsgGroupEnter(void)
{
    SkalPlfInit();
    SkalBlobInit(NULL, 0);
    SkalMsgInit();
    SkalPlfThreadMakeSkal_DEBUG("TestThread@mydomain");
    SkalSetDomain("mydomain");
    return RTTrue;
}

static RTBool testMsgGroupExit(void)
{
    SkalPlfThreadUnmakeSkal_DEBUG();
    SkalMsgExit();
    SkalBlobExit();
    SkalPlfExit();
    return RTTrue;
}


static SkalMsg* gMsg = NULL;
static SkalAlarm* gAlarm1 = NULL;
static SkalAlarm* gAlarm2 = NULL;

RTT_GROUP_START(TestSkalMsg, 0x00040001u, testMsgGroupEnter, testMsgGroupExit)

RTT_TEST_START(skal_should_create_msg)
{
    // Fool skal-msg into thinking this thread is managed by SKAL
    SkalPlfThreadSetSpecific((void*)0xcafedeca);

    gMsg = SkalMsgCreateEx("TestName", "dummy-dst", 0, 19);
    RTT_ASSERT(gMsg != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_name)
{
    const char* name = SkalMsgName(gMsg);
    RTT_ASSERT(name != NULL);
    RTT_EXPECT(strcmp(name, "TestName") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_sender)
{
    const char* sender = SkalMsgSender(gMsg);
    RTT_ASSERT(sender != NULL);
    RTT_EXPECT(strcmp(sender, "TestThread@mydomain") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_recipient)
{
    const char* recipient = SkalMsgRecipient(gMsg);
    RTT_ASSERT(recipient != NULL);
    RTT_EXPECT(strcmp(recipient, "dummy-dst@mydomain") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_have_correct_ttl)
{
    RTT_EXPECT(SkalMsgTtl(gMsg) == 19);
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

RTT_TEST_START(skal_msg_add_blob)
{
    SkalBlob* blob = SkalBlobCreate(NULL, NULL, 500);
    RTT_ASSERT(blob != NULL);
    uint8_t* data = SkalBlobMap(blob);
    RTT_ASSERT(data != NULL);
    strcpy((char*)data, "Hello, World!");
    SkalBlobUnmap(blob);
    SkalMsgAddBlob(gMsg, "TestBlob", blob);
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

RTT_TEST_START(skal_msg_should_have_correct_blob)
{
    RTT_EXPECT(SkalMsgHasBlob(gMsg, "TestBlob"));
    SkalBlob* blob = SkalMsgGetBlob(gMsg, "TestBlob");
    RTT_EXPECT(blob != NULL);
    uint8_t* data = SkalBlobMap(blob);
    RTT_ASSERT(data != NULL);
    RTT_EXPECT(SkalStrcmp((char*)data, "Hello, World!") == 0);
    SkalBlobUnmap(blob);
    SkalBlobUnref(blob);
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
    char timestamp[64];
    SkalPlfTimestamp(SkalMsgTimestamp_us(gMsg), timestamp, sizeof(timestamp));
    SkalBlob* blob = SkalMsgGetBlob(gMsg, "TestBlob");
    char* expected = SkalSPrintf(
        "{\n"
        " \"version\": 1,\n"
        " \"timestamp\": \"%s\"\n"
        " \"name\": \"TestName\",\n"
        " \"sender\": \"TestThread@mydomain\",\n"
        " \"recipient\": \"dummy-dst@mydomain\",\n"
        " \"ttl\": 19,\n"
        " \"flags\": 0,\n"
        " \"iflags\": 0,\n"
        " \"fields\": [\n"
        "  {\n"
        "   \"name\": \"TestBlob\",\n"
        "   \"type\": \"blob\",\n"
        "   \"value\": \"malloc:%p\"\n"
        "  },\n"
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
        "   \"name\": \"alarm1\",\n"
        "   \"severity\": \"notice\",\n"
        "   \"origin\": \"TestThread@mydomain\",\n"
        "   \"isOn\": true,\n"
        "   \"autoOff\": true,\n"
        "   \"timestamp_us\": %lld,\n"
        "   \"comment\": \"\"\n"
        "  },\n"
        "  {\n"
        "   \"name\": \"alarm2\",\n"
        "   \"severity\": \"error\",\n"
        "   \"origin\": \"TestThread@mydomain\",\n"
        "   \"isOn\": false,\n"
        "   \"autoOff\": false,\n"
        "   \"timestamp_us\": %lld,\n"
        "   \"comment\": \"This is a test\"\n"
        "  }\n"
        " ]\n"
        "}\n",
        timestamp,
        blob,
        (long long)SkalAlarmTimestamp_us(gAlarm1),
        (long long)SkalAlarmTimestamp_us(gAlarm2));
    SkalBlobUnref(blob);

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
    const char* s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "alarm1") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_NOTICE);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "TestThread@mydomain") == 0);
    RTT_EXPECT(SkalAlarmIsOn(alarm));
    RTT_EXPECT(SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmComment(alarm) == NULL);
    SkalAlarmUnref(alarm);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_copy_msg)
{
    SkalMsg* msg = SkalMsgCopy(gMsg, "blackhole");
    RTT_EXPECT(SkalMsgTimestamp_us(msg) == SkalMsgTimestamp_us(gMsg));
    const char* s = SkalMsgName(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, SkalMsgName(gMsg)) == 0);
    s = SkalMsgSender(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, SkalMsgSender(gMsg)) == 0);
    s = SkalMsgRecipient(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(SkalStartsWith(s, "blackhole"));
    RTT_EXPECT(SkalMsgFlags(msg) == SkalMsgFlags(gMsg));
    RTT_EXPECT(SkalMsgIFlags(msg) == SkalMsgIFlags(gMsg));
    RTT_EXPECT(SkalMsgTtl(msg) == SkalMsgTtl(gMsg));

    // Test fields

    RTT_EXPECT(SkalMsgHasInt(msg, "TestInt"));
    RTT_EXPECT(SkalMsgGetInt(msg, "TestInt") == -789);

    RTT_EXPECT(SkalMsgHasDouble(msg, "TestDouble"));
    double delta = SkalMsgGetDouble(msg, "TestDouble") - 3.456779999999999972715e+02;
    RTT_EXPECT(delta < (SkalMsgGetDouble(msg, "TestDouble") * 0.0000001));

    RTT_EXPECT(SkalMsgHasString(msg, "TestString"));
    RTT_EXPECT(SkalMsgHasAsciiString(msg, "TestString"));
    s = SkalMsgGetString(msg, "TestString");
    RTT_EXPECT(strcmp(s, "This is a test string") == 0);

    RTT_EXPECT(SkalMsgHasMiniblob(msg, "TestMiniblob"));
    int size_B;
    const uint8_t* miniblob = SkalMsgGetMiniblob(msg, "TestMiniblob", &size_B);
    RTT_EXPECT(miniblob != NULL);
    RTT_EXPECT(4 == size_B);
    uint8_t expected[4] = { 0x11, 0x22, 0x33, 0x44 };
    RTT_EXPECT(memcmp(miniblob, expected, sizeof(expected)) == 0);

    RTT_EXPECT(SkalMsgHasBlob(msg, "TestBlob"));
    SkalBlob* blob = SkalMsgGetBlob(msg, "TestBlob");
    RTT_EXPECT(blob != NULL);
    uint8_t* data = SkalBlobMap(blob);
    RTT_EXPECT(data != NULL);
    RTT_EXPECT(SkalStrcmp((char*)data, "Hello, World!") == 0);
    SkalBlobUnmap(blob);
    SkalBlobUnref(blob);

    // Test alarms
    SkalAlarm* alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(alarm != NULL);
    s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "alarm2") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_ERROR);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "TestThread@mydomain") == 0);
    RTT_EXPECT(!SkalAlarmIsOn(alarm));
    RTT_EXPECT(!SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmTimestamp_us(alarm) == SkalAlarmTimestamp_us(gAlarm2));
    s = SkalAlarmComment(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "This is a test") == 0);
    SkalAlarmUnref(alarm);
    alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(NULL == alarm);

    SkalMsgUnref(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_msg_should_detach_alarm2)
{
    SkalAlarm* alarm = SkalMsgDetachAlarm(gMsg);
    RTT_EXPECT(alarm != NULL);
    const char* s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "alarm2") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_ERROR);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "TestThread@mydomain") == 0);
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
        " \"name\": \"SomeName\",\n"
        " \"sender\": \"Some\\\\Thread@domainA\",\n"
        " \"reci\\pient\": \"you@wonderland\",\n"
        " \"ttl\": 27,\n"
        " \"flags\": 3,\n"
        " \"timestamp\": \"2017-05-15T08:23:26.003456Z\",\n"
        " \"iflags\": 1,\n"
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
        "   \"origin\": \"PanicAttak@wilderness\",\n"
        "   \"isOn\": true,\n"
        "   \"name\": \"AlarmNameA\",\n"
        "   \"autoOff\": false\n"
        "  },\n"
        "  {\n"
        "   \"timestamp_us\": 987654321,\n"
        "   \"severity\": \"warning\",\n"
        "   \"isOn\": false,\n"
        "   \"name\": \"AlarmNameB\",\n"
        "   \"autoOff\": true,\n"
        "   \"comment\": \"This is a \\fake alarm\"\n"
        "  }\n"
        " ]\n"
        "}\n";

    SkalMsg* msg = SkalMsgCreateFromJson(json);
    RTT_EXPECT(msg != NULL);

    RTT_EXPECT(SkalMsgTimestamp_us(msg) == 1494836606003456);

    const char* s = SkalMsgName(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "SomeName") == 0);

    s = SkalMsgSender(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "Some\\Thread@domainA") == 0);

    s = SkalMsgRecipient(msg);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "you@wonderland") == 0);

    uint8_t i = SkalMsgFlags(msg);
    RTT_EXPECT(SKAL_MSG_FLAG_UDP == i);

    i = SkalMsgIFlags(msg);
    RTT_EXPECT(SKAL_MSG_IFLAG_INTERNAL == i);

    RTT_EXPECT(SkalMsgTtl(msg) == 27);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeDouble"));
    double d = SkalMsgGetDouble(msg, "SomeDouble");
    double diff = d - 345.678;
    if (diff < 0) {
        diff = -diff;
    }
    RTT_EXPECT(diff < 0.00001);

    RTT_EXPECT(SkalMsgHasField(msg, "SomeMiniblob"));
    int size_B;
    const uint8_t* data = SkalMsgGetMiniblob(msg, "SomeMiniblob", &size_B);
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
    s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "AlarmNameA") == 0);
    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_NOTICE);
    s = SkalAlarmOrigin(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "PanicAttak@wilderness") == 0);
    RTT_EXPECT(SkalAlarmIsOn(alarm));
    RTT_EXPECT(!SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmTimestamp_us(alarm) == 1234567890LL);
    RTT_EXPECT(SkalAlarmComment(alarm) == NULL);
    SkalAlarmUnref(alarm);

    alarm = SkalMsgDetachAlarm(msg);
    RTT_EXPECT(alarm != NULL);
    s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "AlarmNameB") == 0);
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

RTT_TEST_START(skal_should_survive_invalid_json1)
{
    const char* json =
        "{\n"
        " \"name\": \"SomeName\",\n"
        " \"sender\": \"Some\\\\Thread@domainA\",\n"
        " \"reci\\pient\": \"you@wonderland\",\n"
        " \"ttl\": 27,\n"
        " \"flags\": 3,\n"
        " \"iflags\": 128,\n"
        " \"version\": 1,\n"
        " \"timestamp\": \"2017-05-15T08:23:26.003456Z\",\n"
        " \"fields\": [\n"
        "  {\n"
        "   \"name\": \"Some\"Double\",\n"
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
        "   \"origin\": \"PanicAttak@wilderness\",\n"
        "   \"isOn\": true,\n"
        "   \"name\": \"AlarmNameA\",\n"
        "   \"autoOff\": false\n"
        "  },\n"
        "  {\n"
        "   \"timestamp_us\": 987654321,\n"
        "   \"severity\": \"warning\",\n"
        "   \"isOn\": false,\n"
        "   \"name\": \"AlarmNameB\",\n"
        "   \"autoOff\": true,\n"
        "   \"comment\": \"This is a \\fake alarm\"\n"
        "  }\n"
        " ]\n"
        "}\n";

    SkalLogEnable(false);
    SkalMsg* msg = SkalMsgCreateFromJson(json);
    SkalLogEnable(true);
    RTT_EXPECT(NULL == msg);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalMsg,
        skal_should_create_msg,
        skal_msg_should_have_correct_name,
        skal_msg_should_have_correct_sender,
        skal_msg_should_have_correct_recipient,
        skal_msg_should_have_correct_ttl,
        skal_msg_add_int,
        skal_msg_add_double,
        skal_msg_add_string,
        skal_msg_add_miniblob,
        skal_msg_add_blob,
        skal_msg_should_have_correct_int,
        skal_msg_should_have_correct_double,
        skal_msg_should_have_correct_string,
        skal_msg_should_have_correct_miniblob,
        skal_msg_should_have_correct_blob,
        skal_msg_should_attach_alarms,
        skal_msg_should_produce_correct_json,
        skal_msg_should_detach_alarm1,
        skal_msg_should_copy_msg,
        skal_msg_should_detach_alarm2,
        skal_msg_should_have_no_more_alarms,
        skal_msg_should_free,
        skal_should_have_no_more_msg_ref_1,
        skal_msg_should_create_from_json,
        skal_should_survive_invalid_json1)
