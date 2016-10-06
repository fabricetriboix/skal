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
#include <stdlib.h>
#include <string.h>



/*----------------+
 | Macros & Types |
 +----------------*/


struct SkalAlarm {
    int               ref;
    char              type[SKAL_NAME_MAX];
    SkalAlarmSeverity severity;
    char              origin[SKAL_NAME_MAX];
    bool              isOn;
    bool              autoOff;
    int64_t           timestamp_us;
    char*             comment;
};



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalAlarm* SkalAlarmCreate(const char* type, SkalAlarmSeverity severity,
        bool isOn, bool autoOff, const char* format, ...)
{
    SKALASSERT(SkalIsAsciiString(type, SKAL_NAME_MAX));

    SkalAlarm* alarm = SkalMallocZ(sizeof(*alarm));
    alarm->ref = 1;
    strncpy(alarm->type, type, sizeof(alarm->type) - 1);
    alarm->severity = severity;
    if (SkalPlfThreadGetSpecific() != NULL) {
        // The current thread is managed by SKAL
        SkalPlfThreadGetName(alarm->origin, sizeof(alarm->origin));
    }
    alarm->isOn = isOn;
    alarm->autoOff = autoOff;
    alarm->timestamp_us = SkalPlfNow_us();
    if (format != NULL) {
        va_list ap;
        va_start(ap, format);
        alarm->comment = SkalVSPrintf(format, ap);
        va_end(ap);
    }
    return alarm;
}


void SkalAlarmRef(SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    alarm->ref++;
}


void SkalAlarmUnref(SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    alarm->ref--;
    if (alarm->ref <= 0) {
        free(alarm->comment);
        free(alarm);
    }
}


const char* SkalAlarmType(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->type;
}


SkalAlarmSeverity SkalAlarmGetSeverity(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->severity;
}


const char* SkalAlarmOrigin(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    const char* origin = NULL;
    if (alarm->origin[0] != '\0') {
        origin = alarm->origin;
    }
    return origin;
}


bool SkalAlarmIsOn(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->isOn;
}


bool SkalAlarmAutoOff(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->autoOff;
}


int64_t SkalAlarmTimestamp_us(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->timestamp_us;
}


const char* SkalAlarmComment(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->comment;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/

