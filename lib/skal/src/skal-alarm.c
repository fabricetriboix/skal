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
    char*              name;
    SkalAlarmSeverityE severity;
    char*              origin;
    bool               isOn;
    bool               autoOff;
    int64_t            timestamp_us;
    char*              comment;
};


typedef enum {
    SKAL_ALARM_PROPERTY_INVALID,
    SKAL_ALARM_PROPERTY_NAME,
    SKAL_ALARM_PROPERTY_SEVERITY,
    SKAL_ALARM_PROPERTY_ORIGIN,
    SKAL_ALARM_PROPERTY_ISON,
    SKAL_ALARM_PROPERTY_AUTOOFF,
    SKAL_ALARM_PROPERTY_TIMESTAMP,
    SKAL_ALARM_PROPERTY_COMMENT
} skalAlarmProperty;



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


/** Parse a JSON string, taking care of escaped characters
 *
 * @param json [in]  JSON text to parse; must not be NULL
 * @param str  [out] The parsed string; must not be NULL; please call `free(3)`
 *                   on it when finished
 *
 * @return Pointer to the character in `json` just after the parsed string, or
 *         NULL if invalid JSON (in such a case, `*str` will be set to NULL)
 */
static const char* skalAlarmParseJsonString(const char* json, char** str);


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


/** Parse the given string as a JSON alarm property
 *
 * @param str [in] String to parse; must not be NULL
 *
 * @return The parsed alarm property; SKAL_ALARM_PROPERTY_INVALID if `str` is
 *         invalid
 */
static skalAlarmProperty skalAlarmStrToProp(const char* str);



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalAlarm* SkalAlarmCreate(const char* name, SkalAlarmSeverityE severity,
        bool isOn, bool autoOff, const char* format, ...)
{
    SKALASSERT(name != NULL);

    SkalAlarm* alarm = SkalMallocZ(sizeof(*alarm));
    alarm->ref = 1;
    alarm->name = SkalStrdup(name);
    alarm->severity = severity;
    if (SkalPlfThreadGetSpecific() != NULL) {
        // The current thread is managed by SKAL
        alarm->origin = SkalStrdup(SkalPlfThreadGetName());
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
        free(alarm->name);
        free(alarm->origin);
        free(alarm->comment);
        free(alarm);
    }
}


const char* SkalAlarmName(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->name;
}


SkalAlarmSeverityE SkalAlarmSeverity(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->severity;
}


const char* SkalAlarmOrigin(const SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    return alarm->origin;
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
            "%s \"name\": \"%s\",\n"
            "%s \"severity\": \"%s\",\n"
            "%s \"origin\": \"%s\",\n"
            "%s \"isOn\": %s,\n"
            "%s \"autoOff\": %s,\n"
            "%s \"timestamp_us\": %lld,\n"
            "%s \"comment\": \"%s\"\n"
            "%s}",
            indent,
            indent, alarm->name,
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


SkalAlarm* SkalAlarmCopy(SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    SkalAlarm* copy = SkalMallocZ(sizeof(*copy));
    copy->ref = 1;
    copy->name = SkalStrdup(alarm->name);
    copy->severity = alarm->severity;
    copy->origin = SkalStrdup(alarm->origin);
    copy->isOn = alarm->isOn;
    copy->autoOff = alarm->autoOff;
    copy->timestamp_us = alarm->timestamp_us;
    copy->comment = SkalStrdup(alarm->comment);
    return copy;
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


static const char* skalAlarmParseJsonString(const char* json, char** str)
{
    SKALASSERT(str != NULL);
    *str = NULL;

    json = skalAlarmSkipSpaces(json);
    if (*json != '"') {
        SkalLog("SkalAlarm: Invalid JSON alarm object: expected '\"' character");
        return NULL;
    }
    json++;

    SkalStringBuilder* sb = SkalStringBuilderCreate(1024);
    while ((*json != '\0') && (*json != '"')) {
        if ('\\' == *json) {
            json++;
            if ('\0' == *json) {
                SkalLog("SkalAlarm: Invalid JSON alarm object: null character after \\");
                char* tmp = SkalStringBuilderFinish(sb);
                free(tmp);
                return NULL;
            }
        }
        SkalStringBuilderAppend(sb, "%c", *json);
        json++;
    }

    char* s = SkalStringBuilderFinish(sb);
    if ('\0' == *json) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: unterminated string");
        free(s);
        return NULL;
    }
    SKALASSERT('"' == *json);
    json++;

    *str = s;
    return json;
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
        SkalLog("SkalAlarm: Invalid JSON alarm object: expected 'true' or 'false'");
        return NULL;
    }
    return json;
}


static const char* skalAlarmParseJson(const char* json, SkalAlarm* alarm)
{
    if (*json != '{') {
        SkalLog("SkalAlarm: Invalid JSON alarm object: expected '{' character");
        return NULL;
    }
    json++;
    json = skalAlarmSkipSpaces(json);

    bool severityParsed = false;
    bool isOnParsed = false;
    bool autoOffParsed = false;
    bool timestampParsed = false;

    while ((*json != '\0') && (*json != '}')) {
        // Parse property name
        char* str;
        json = skalAlarmParseJsonString(json, &str);
        if (NULL == json) {
            return NULL;
        }
        skalAlarmProperty property = skalAlarmStrToProp(str);
        if (SKAL_ALARM_PROPERTY_INVALID == property) {
            SkalLog("SkalAlarm: Invalid JSON: Unknown property '%s'", str);
            free(str);
            return NULL;
        }
        free(str);

        json = skalAlarmSkipSpaces(json);
        if (*json != ':') {
            SkalLog("SkalAlarm: Invalid JSON: expected ':' character");
            return NULL;
        }
        json++;
        json = skalAlarmSkipSpaces(json);

        // Parse property value
        switch (property) {
        case SKAL_ALARM_PROPERTY_NAME :
            json = skalAlarmParseJsonString(json, &alarm->name);
            break;

        case SKAL_ALARM_PROPERTY_SEVERITY :
            json = skalAlarmParseJsonString(json, &str);
            if (NULL == json) {
                return NULL;
            }
            SKALASSERT(str != NULL);
            if (strcmp(str, "notice") == 0) {
                alarm->severity = SKAL_ALARM_NOTICE;
            } else if (strcmp(str, "warning") == 0) {
                alarm->severity = SKAL_ALARM_WARNING;
            } else if (strcmp(str, "error") == 0) {
                alarm->severity = SKAL_ALARM_ERROR;
            } else {
                SkalLog("SkalAlarm: Invalid JSON alarm object: unknown alarm severity: \"%s\"", str);
                free(str);
                return NULL;
            }
            free(str);
            severityParsed = true;
            break;

        case SKAL_ALARM_PROPERTY_ORIGIN :
            json = skalAlarmParseJsonString(json, &alarm->origin);
            break;

        case SKAL_ALARM_PROPERTY_ISON :
            json = skalAlarmParseJsonBool(json, &alarm->isOn);
            isOnParsed = true;
            break;

        case SKAL_ALARM_PROPERTY_AUTOOFF :
            json = skalAlarmParseJsonBool(json, &alarm->autoOff);
            autoOffParsed = true;
            break;

        case SKAL_ALARM_PROPERTY_TIMESTAMP :
            {
                long long tmp;
                if (sscanf(json, "%lld", &tmp) != 1) {
                    SkalLog("SkalAlarm: Invalid JSON alarm object: can't parse timestamp");
                    return NULL;
                }
                alarm->timestamp_us = tmp;
                while ((*json != '\0') && (*json != ',') && (*json != '}')) {
                    json++;
                }
                if ('\0' == *json) {
                    SkalLog("SkalAlarm: Invalid JSON alarm object: expected ',' or '}' after number");
                    return NULL;
                }
                timestampParsed = true;
            }
            break;

        case SKAL_ALARM_PROPERTY_COMMENT :
            json = skalAlarmParseJsonString(json, &alarm->comment);
            break;

        default :
            SKALPANIC_MSG("Internal bug! Fix me!");
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
        SkalLog("SkalAlarm: Invalid JSON alarm object: expected '}'");
        return NULL;
    }
    SKALASSERT('}' == *json);
    json++;

    // Check that we have all properties required to define an alarm
    if (NULL == alarm->name) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: \"name\" required");
        return NULL;
    }
    if (!severityParsed) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: \"severity\" required");
        return NULL;
    }
    if (!isOnParsed) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: \"isOn\" required");
        return NULL;
    }
    if (!autoOffParsed) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: \"autoOff\" required");
        return NULL;
    }
    if (!timestampParsed) {
        SkalLog("SkalAlarm: Invalid JSON alarm object: \"timestamp_us\" required");
        return NULL;
    }

    return json;
}


static skalAlarmProperty skalAlarmStrToProp(const char* str)
{
    SKALASSERT(str != NULL);
    skalAlarmProperty property = SKAL_ALARM_PROPERTY_INVALID;
    if (strcmp(str, "name") == 0) {
        property = SKAL_ALARM_PROPERTY_NAME;
    } else if (strcmp(str, "severity") == 0) {
        property = SKAL_ALARM_PROPERTY_SEVERITY;
    } else if (strcmp(str, "origin") == 0) {
        property = SKAL_ALARM_PROPERTY_ORIGIN;
    } else if (strcmp(str, "isOn") == 0) {
        property = SKAL_ALARM_PROPERTY_ISON;
    } else if (strcmp(str, "autoOff") == 0) {
        property = SKAL_ALARM_PROPERTY_AUTOOFF;
    } else if (strcmp(str, "timestamp_us") == 0) {
        property = SKAL_ALARM_PROPERTY_TIMESTAMP;
    } else if (strcmp(str, "comment") == 0) {
        property = SKAL_ALARM_PROPERTY_COMMENT;
    }
    return property;
}
