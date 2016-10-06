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

#include "skal.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static RTBool testAlarmGroupEnter(void)
{
    SkalPlfInit();
    return RTTrue;
}

static RTBool testAlarmGroupExit(void)
{
    SkalPlfExit();
    return RTTrue;
}


static SkalAlarm* gAlarm = NULL;

RTT_GROUP_START(TestSkalAlarm, 0x00050001u,
        testAlarmGroupEnter, testAlarmGroupExit)

RTT_TEST_START(skal_should_create_alarm)
{
    SkalPlfThreadSetName("TestThreadA");

    // Fool skal-alarm into thinking this thread is managed by SKAL
    SkalPlfThreadSetSpecific((void*)0xcafedeca);

    gAlarm = SkalAlarmCreate("total-meltdown", SKAL_ALARM_WARNING, true, false,
            "Hello, %s! %d", "world", 9);
    RTT_ASSERT(gAlarm != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_type)
{
    const char* type = SkalAlarmType(gAlarm);
    RTT_EXPECT(type != NULL);
    RTT_EXPECT(strcmp(type, "total-meltdown") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_alarm_should_have_correct_severity)
{
    RTT_EXPECT(SkalAlarmGetSeverity(gAlarm) == SKAL_ALARM_WARNING);
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

RTT_TEST_START(skal_alarm_should_free)
{
    SkalAlarmUnref(gAlarm);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalAlarm,
        skal_should_create_alarm,
        skal_alarm_should_have_correct_type,
        skal_alarm_should_have_correct_severity,
        skal_alarm_should_have_correct_origin,
        skal_alarm_should_have_correct_ison,
        skal_alarm_should_have_correct_autooff,
        skal_alarm_should_have_correct_timestamp,
        skal_alarm_should_have_correct_comment,
        skal_alarm_should_free)
