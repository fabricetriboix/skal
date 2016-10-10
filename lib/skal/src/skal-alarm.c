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

#include "skal-alarm.h"
#include "cdslist.h"
#include <stdlib.h>
#include <string.h>
#include <ctype.h>



/*----------------+
 | Macros & Types |
 +----------------*/


struct SkalAlarm {
    CdsListItem        item;
    int                ref;
    char               type[SKAL_NAME_MAX];
    SkalAlarmSeverityE severity;
    char               origin[SKAL_NAME_MAX];
    bool               isOn;
    bool               autoOff;
    int64_t            timestamp_us;
    char*              comment;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Skip any blank characters in a string
 *
 * @param str [in] String in question; must not be NULL
 *
 * @return Pointer inside `str` after any blank characters at the beginning
 */
static const char* skalAlarmSkipSpaces(const char* str);


/** Parse a JSON string
 *
 * @param json   [in]  JSON text to parse; must not be NULL
 * @param buffer [out] Where to write the parsed string; must not be NULL
 * @param size_B [in]  Size of the above buffer, in bytes; must be >0
 *
 * @return The character in `json` just after the parsed string, or NULL if
 *         invalid JSON
 */
static const char* skalAlarmParseJsonString(const char* json,
        char* buffer, int size_B);


/** Compute the number of bytes in a JSON string
 *
 * @return The number of bytes, or -1 if invalid JSON
 */
static int skalAlarmGetJsonStringLength(const char* json);


/** Parse a JSON boolean value
 *
 * @param json [in]  JSON text to parse; must not be NULL
 * @param b    [out] Where to write the parsed boolean; must not be NULL
 *
 * @return The character in `json` just after the parsed boolean, or NULL if
 *         invalid JSON
 */
static const char* skalAlarmParseJsonBool(const char* json, bool* b);


/** Parse the given JSON text as an alarm structure
 *
 * @param json  [in]     JSON text to parse
 * @param alarm [in,out] Alarm structure to update
 *
 * @return The character in `json` just after the parsed alarm object, or NULL
 *         in case of invalid JSON
 */
static const char* skalAlarmParseJson(const char* json, SkalAlarm* alarm);



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalAlarm* SkalAlarmCreate(const char* type, SkalAlarmSeverityE severity,
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


SkalAlarmSeverityE SkalAlarmSeverity(const SkalAlarm* alarm)
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


char* SkalAlarmToJson(const SkalAlarm* alarm, int nindent)
{
    SKALASSERT(alarm != NULL);

    if (nindent < 0) {
        nindent = 0;
    }
    char* indent = SkalMalloc(nindent + 1);
    for (int i = 0; i < nindent; i++) {
        indent[i] = ' ';
    }
    indent[nindent] = '\0';

    const char* severity = NULL;
    switch (alarm->severity) {
    case SKAL_ALARM_NOTICE  : severity = "notice";  break;
    case SKAL_ALARM_WARNING : severity = "warning"; break;
    case SKAL_ALARM_ERROR   : severity = "error";   break;
    }
    SKALASSERT(severity != NULL);

    const char* origin = alarm->origin;
    if (NULL == origin) {
        origin = "";
    }

    const char* comment = alarm->comment;
    if (NULL == comment) {
        comment = "";
    }

    char* json = SkalSPrintf(
            "%s{\n"
            "%s \"type\": \"%s\",\n"
            "%s \"severity\": \"%s\",\n"
            "%s \"origin\": \"%s\",\n"
            "%s \"isOn\": %s,\n"
            "%s \"autoOff\": %s,\n"
            "%s \"timestamp_us\": %lld,\n"
            "%s \"comment\": \"%s\"\n"
            "%s}",
            indent,
            indent, alarm->type,
            indent, severity,
            indent, origin,
            indent, alarm->isOn ? "true" : "false",
            indent, alarm->autoOff ? "true" : "false",
            indent, (long long)alarm->timestamp_us,
            indent, comment,
            indent);

    free(indent);
    return json;
}


SkalAlarm* SkalAlarmCreateFromJson(const char** pJson)
{
    SKALASSERT(pJson != NULL);
    const char* json = *pJson;
    SKALASSERT(json != NULL);

    json = skalAlarmSkipSpaces(json);
    SkalAlarm* alarm = SkalMallocZ(sizeof(*alarm));
    alarm->ref = 1;
    json = skalAlarmParseJson(json, alarm);
    if (NULL == json) {
        SkalAlarmUnref(alarm);
        alarm = NULL;
    } else {
        *pJson = json;
    }

    return alarm;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static const char* skalAlarmSkipSpaces(const char* str)
{
    while ((*str != '\0') && isspace(*str)) {
        str++;
    }
    return str;
}


static const char* skalAlarmParseJsonString(const char* json,
        char* buffer, int size_B)
{
    json = skalAlarmSkipSpaces(json);
    if (*json != '"') {
        SkalLog("Invalid JSON alarm object: expected '\"' character");
        return NULL;
    }
    json++;

    int count = 0;
    while ((*json != '\0') && (*json != '"')) {
        if ('\\' == *json) {
            json++;
            if ('\0' == *json) {
                SkalLog("Invalid JSON alarm object: null character after \\");
                return NULL;
            }
        }
        buffer[count] = *json;
        count++;
        json++;
        if ((count >= size_B) && (*json != '"')) {
            SkalLog("Invalid JSON alarm object: string too long (expected max %d chars)",
                    size_B);
            return NULL;
        }
    }

    if ('\0' == *json) {
        SkalLog("Invalid JSON alarm object: no '\"' character terminating a string");
        return NULL;
    }
    SKALASSERT('"' == *json);
    json++;

    buffer[count] = '\0';
    return json;
}


static int skalAlarmGetJsonStringLength(const char* json)
{
    json = skalAlarmSkipSpaces(json);
    if (*json != '"') {
        SkalLog("Invalid JSON alarm object: expected '\"' character");
        return -1;
    }
    json++;

    int count = 0;
    while ((*json != '\0') && (*json != '"')) {
        if ('\\' == *json) {
            json++;
            if ('\0' == *json) {
                SkalLog("Invalid JSON alarm object: null character after \\");
                return -1;
            }
        }
        count++;
        json++;
    }

    if ('\0' == *json) {
        SkalLog("Invalid JSON alarm object: no '\"' character terminating a string");
        return -1;
    }
    SKALASSERT('"' == *json);
    return count;
}


static const char* skalAlarmParseJsonBool(const char* json, bool* b)
{
    json = skalAlarmSkipSpaces(json);
    if (strncmp(json, "true", 4) == 0) {
        *b = true;
        json += 4;
    } else if (strncmp(json, "false", 5) == 0) {
        *b = false;
        json += 5;
    } else {
        SkalLog("Invalid JSON alarm object: expected 'true' or 'false'");
        return NULL;
    }
    return json;
}


static const char* skalAlarmParseJson(const char* json, SkalAlarm* alarm)
{
    if (*json != '{') {
        SkalLog("Invalid JSON alarm object: expected '{' character");
        return NULL;
    }
    json++;
    json = skalAlarmSkipSpaces(json);

    bool severityParsed = false;
    bool isOnParsed = false;
    bool autoOffParsed = false;
    bool timestampParsed = false;

    char buffer[SKAL_NAME_MAX];
    while ((*json != '\0') && (*json != '}')) {
        // Parse property name
        json = skalAlarmParseJsonString(json, buffer, sizeof(buffer));
        if (NULL == json) {
            return NULL;
        }
        json = skalAlarmSkipSpaces(json);
        if (*json != ':') {
            SkalLog("Invalid JSON: expected ':' character");
            return NULL;
        }
        json++;
        json = skalAlarmSkipSpaces(json);

        // Parse property value
        if (strcmp(buffer, "type") == 0) {
            json = skalAlarmParseJsonString(json,
                    alarm->type, sizeof(alarm->type));

        } else if (strcmp(buffer, "severity") == 0) {
            json = skalAlarmParseJsonString(json, buffer, sizeof(buffer));
            if (NULL == json) {
                return NULL;
            }
            if (strcmp(buffer, "notice") == 0) {
                alarm->severity = SKAL_ALARM_NOTICE;
            } else if (strcmp(buffer, "warning") == 0) {
                alarm->severity = SKAL_ALARM_WARNING;
            } else if (strcmp(buffer, "error") == 0) {
                alarm->severity = SKAL_ALARM_ERROR;
            } else {
                SkalLog("Invalid JSON alarm object: unknown alarm severity: \"%s\"", buffer);
                return NULL;
            }
            severityParsed = true;

        } else if (strcmp(buffer, "origin") == 0) {
            json = skalAlarmParseJsonString(json,
                    alarm->origin, sizeof(alarm->origin));

        } else if (strcmp(buffer, "isOn") == 0) {
            json = skalAlarmParseJsonBool(json, &alarm->isOn);
            isOnParsed = true;

        } else if (strcmp(buffer, "autoOff") == 0) {
            json = skalAlarmParseJsonBool(json, &alarm->autoOff);
            autoOffParsed = true;

        } else if (strcmp(buffer, "timestamp_us") == 0) {
            long long tmp;
            if (sscanf(json, "%lld", &tmp) != 1) {
                return NULL;
            }
            alarm->timestamp_us = tmp;
            while ((*json != '\0') && (*json != ',') && (*json != '}')) {
                json++;
            }
            if ('\0' == *json) {
                SkalLog("Invalid JSON alarm object: expected ',' or '}' after number");
                return NULL;
            }
            timestampParsed = true;

        } else if (strcmp(buffer, "comment") == 0) {
            int length = skalAlarmGetJsonStringLength(json);
            if (length < 0) {
                return NULL;
            }
            free(alarm->comment);
            alarm->comment = SkalMalloc(length + 1);
            json = skalAlarmParseJsonString(json, alarm->comment, length + 1);
            if (NULL == json) {
                return NULL;
            }
            alarm->comment[length] = '\0';

        } else {
            SkalLog("Invalid JSON alarm object: Unknown alarm object property: \"%s\"",
                    buffer);
            return NULL;
        }

        if (NULL == json) {
            return NULL;
        }

        json = skalAlarmSkipSpaces(json);
        if (',' == *json) {
            json++;
            json = skalAlarmSkipSpaces(json);
        }
    }

    if ('\0' == *json) {
        SkalLog("Invalid JSON alarm object: expected '}'");
        return NULL;
    }
    SKALASSERT('}' == *json);
    json++;

    // Check that we have all properties required to define an alarm
    if ('\0' == alarm->type[0]) {
        SkalLog("Invalid JSON alarm object: \"type\" required");
        return NULL;
    }
    if (!severityParsed) {
        SkalLog("Invalid JSON alarm object: \"severity\" required");
        return NULL;
    }
    if (!isOnParsed) {
        SkalLog("Invalid JSON alarm object: \"isOn\" required");
        return NULL;
    }
    if (!autoOffParsed) {
        SkalLog("Invalid JSON alarm object: \"autoOff\" required");
        return NULL;
    }
    if (!timestampParsed) {
        SkalLog("Invalid JSON alarm object: \"timestamp_us\" required");
        return NULL;
    }

    return json;
}
