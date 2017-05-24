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

#include "skal-alarm.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool testAlarmGroupEnter(void)
{
    SkalPlfInit();
    SkalPlfThreadMakeSkal_DEBUG("TestThreadA");
    return RTTrue;
}

static RTBool testAlarmGroupExit(void)
{
    SkalPlfThreadUnmakeSkal_DEBUG();
    SkalPlfExit();
    return RTTrue;
}


static SkalAlarm* gAlarm = NULL;

RTT_GROUP_START(TestSkalAlarm, 0x00050001u,
        testAlarmGroupEnter, testAlarmGroupExit)

RTT_TEST_START(skal_should_create_alarm)
{
    // Fool skal-alarm into thinking this thread is managed by SKAL
    SkalPlfThreadSetSpecific((void*)0xcafedeca);

    gAlarm = SkalAlarmCreate("total-meltdown", SKAL_ALARM_WARNING, true, false,
            "Hello, %s! %d", "world", 9);
    RTT_ASSERT(gAlarm != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_name)
{
    const char* name = SkalAlarmName(gAlarm);
    RTT_EXPECT(name != NULL);
    RTT_EXPECT(strcmp(name, "total-meltdown") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_severity)
{
    RTT_EXPECT(SkalAlarmSeverity(gAlarm) == SKAL_ALARM_WARNING);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_origin)
{
    const char* origin = SkalAlarmOrigin(gAlarm);
    RTT_EXPECT(origin != NULL);
    RTT_EXPECT(strcmp(origin, "TestThreadA") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_ison)
{
    RTT_EXPECT(SkalAlarmIsOn(gAlarm));
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_autooff)
{
    RTT_EXPECT(!SkalAlarmAutoOff(gAlarm));
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_timestamp)
{
    int64_t now_us = SkalPlfNow_us();
    RTT_EXPECT((now_us - SkalAlarmTimestamp_us(gAlarm)) < 500);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_comment)
{
    const char* comment = SkalAlarmComment(gAlarm);
    RTT_EXPECT(comment != NULL);
    RTT_EXPECT(strcmp(comment, "Hello, world! 9") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_convert_to_json)
{
    char* expected = SkalSPrintf(
        "  {\n"
        "   \"name\": \"total-meltdown\",\n"
        "   \"severity\": \"warning\",\n"
        "   \"origin\": \"TestThreadA\",\n"
        "   \"isOn\": true,\n"
        "   \"autoOff\": false,\n"
        "   \"timestamp_us\": %lld,\n"
        "   \"comment\": \"Hello, world! 9\"\n"
        "  }",
        (long long)SkalAlarmTimestamp_us(gAlarm));
    char* json = SkalAlarmToJson(gAlarm, 2);
    RTT_EXPECT(json != NULL);
    RTT_EXPECT(strcmp(json, expected) == 0);
    free(expected);
    free(json);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_free)
{
    SkalAlarmUnref(gAlarm);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_parse_json)
{
    const char* json =
        "  {\n"
        "   \"severity\": \"err\\or\","
        "   \"isOn\": false,\n"
        "   \"comment\": \"this is\\ a test\",\n"
        "   \"autoOff\": true,\n"
        "     \"timestamp\\_us\": 1234567,\n"
        "   \"name\": \"Bla bla bla\"\n"
        "  },\n";

    SkalAlarm* alarm = SkalAlarmCreateFromJson(&json);
    RTT_EXPECT(alarm != NULL);

    const char* s = SkalAlarmName(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "Bla bla bla") == 0);

    RTT_EXPECT(SkalAlarmSeverity(alarm) == SKAL_ALARM_ERROR);
    RTT_EXPECT(SkalAlarmOrigin(alarm) == NULL);
    RTT_EXPECT(!SkalAlarmIsOn(alarm));
    RTT_EXPECT(SkalAlarmAutoOff(alarm));
    RTT_EXPECT(SkalAlarmTimestamp_us(alarm) == 1234567);

    s = SkalAlarmComment(alarm);
    RTT_EXPECT(s != NULL);
    RTT_EXPECT(strcmp(s, "this is a test") == 0);

    SkalAlarmUnref(alarm);

    RTT_EXPECT(',' == *json);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalAlarm,
        skal_should_create_alarm,
        skal_alarm_should_have_correct_name,
        skal_alarm_should_have_correct_severity,
        skal_alarm_should_have_correct_origin,
        skal_alarm_should_have_correct_ison,
        skal_alarm_should_have_correct_autooff,
        skal_alarm_should_have_correct_timestamp,
        skal_alarm_should_have_correct_comment,
        skal_alarm_should_convert_to_json,
        skal_alarm_should_free,
        skal_alarm_should_parse_json)
