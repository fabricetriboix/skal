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
#include "cdslist.h"
#include "cdsmap.h"
#include <stdlib.h>
#include <stdarg.h>
#include <string.h>
#include <ctype.h>
#include <float.h>



/*----------------+
 | Macros & Types |
 +----------------*/


/** Capacity increment of a JSON string: 16KiB */
#define SKAL_JSON_INITIAL_CAPACITY (16 * 1024)


typedef enum {
    SKAL_MSG_FIELD_PROPERTY_INVALID,
    SKAL_MSG_FIELD_PROPERTY_NAME,
    SKAL_MSG_FIELD_PROPERTY_TYPE,
    SKAL_MSG_FIELD_PROPERTY_VALUE
} skalMsgFieldProperty;


typedef enum {
    SKAL_MSG_FIELD_TYPE_NOTHING,
    SKAL_MSG_FIELD_TYPE_INT,
    SKAL_MSG_FIELD_TYPE_DOUBLE,
    SKAL_MSG_FIELD_TYPE_STRING,
    SKAL_MSG_FIELD_TYPE_MINIBLOB,
    SKAL_MSG_FIELD_TYPE_BLOB
} skalMsgFieldType;


/** Item of map `SkalMsg.fields`
 *
 * We do not keep track of reference count, because a field is only ever
 * reference by one message.
 */
typedef struct {
    CdsMapItem       item;
    skalMsgFieldType type;
    int              size_B; // Used only for strings and miniblobs
    char*            name;
    union {
        int64_t   i;
        double    d;
        char*     s;
        uint8_t*  miniblob;
        SkalBlob* blob;
    };
} skalMsgField;


struct SkalMsg {
    CdsListItem item;         // SKAL messages can be enqueued and dequeued
    int64_t     timestamp_us; // When has this message has been created
    int         ref;
    int         version;
    uint8_t     flags;
    uint8_t     iflags;
    int8_t      ttl;
    char*       name;
    char*       sender;
    char*       recipient;
    CdsMap*     fields; // Map of `skalMsgField`, indexed by field name
    CdsList*    alarms; // List of `SkalAlarm`
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Set a thread name, appending the local domain if no domain is specified
 *
 * @param name [in] Name to set; must not be NULL
 *
 * @return The full thread name; this function never returns NULL; please call
 *         `free(3)` on it when finished.
 */
static char* skalMsgThreadName(const char* name);


/** Allocate a message field and add it to the `msg`
 *
 * @param msg  [in,out] Message the field will be added to; must not be NULL
 * @param name [in]     Field name; must not be NULL
 * @param type [in]     Field type
 *
 * @return The newly created field; this function never returns NULL
 */
static skalMsgField* skalMsgFieldAllocate(SkalMsg* msg,
        const char* name, skalMsgFieldType type);


/** Function to unreference a field in a message field map */
static void skalFieldMapUnref(CdsMapItem* item);


/** Function to append a field to a JSON string representing a message */
static void skalFieldToJson(SkalStringBuilder* sb,
        const char* name, const skalMsgField* field);


/** Function to append an alarm to a JSON string representing a message */
static void skalAlarmToJson(SkalStringBuilder* sb, SkalAlarm* alarm);


/** Try to parse a JSON text into a message
 *
 * @param json [in]     JSON text to parse; must not be NULL
 * @param msg  [in,out] SKAL msg to update
 *
 * @return `true` if OK, `false` if `json` is invalid or wrong version
 */
static bool skalMsgParseJson(const char* json, SkalMsg* msg);


/** Try to parse the given JSON text as the `name` message field
 *
 * @param json [in]     JSON text to parse; must not be NULL
 * @param name [in]     Name of the message field; must not be NULL
 * @param msg  [in,out] SKAL msg to update; must not be NULL
 *
 * @return Pointer to the JSON text after the property value (and the comma if
 *         present), or NULL if error
 */
static const char* skalMsgParseJsonProperty(const char* json,
        const char* name, SkalMsg* msg);


/** Try to parse the given JSON text as a message field
 *
 * @param json  [in]     JSON text to parse; must not be NULL
 * @param field [in,out] Field to update; must not be NULL
 *
 * @return Pointer to the JSON text after the field sub-object (and the comma if
 *         present), or NULL if error
 */
static const char* skalMsgParseJsonField(const char* json, skalMsgField* field);


/** Parse a JSON string, taking care of escaped characters
 *
 * A JSON string is enclosed by double-quote characters, and characters escaped
 * by the backslash character are reproduced verbatim (mainly useful to have
 * double-quote characters in the string).
 *
 * @param json [in]  JSON text to parse; must not be NULL
 * @param str  [out] The parsed string; must not be NULL; please call `free(3)`
 *                   on it when finished
 *
 * @return Pointer to the character in `json` just after the parsed string, or
 *         NULL if invalid JSON (in such a case, `*str` will be set to NULL)
 */
static const char* skalMsgParseJsonString(const char* json, char** str);


/** Convert a field property string to enum
 *
 * @param str [in] String to parse; must not be NULL
 *
 * @return The parsed field property; SKAL_MSG_FIELD_PROPERTY_INVALID if `str`
 *         is invalid
 */
static skalMsgFieldProperty skalMsgFieldStrToProp(const char* str);



/*------------------+
 | Global variables |
 +------------------*/


/** Domain name */
static char* gDomain = NULL;


/** Number of message references in this process */
static int64_t gMsgRefCount_DEBUG = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalMsgInit(void)
{
    gDomain = SkalStrdup("^INVAL^");
}


void SkalMsgExit(void)
{
    free(gDomain);
}


SkalMsg* SkalMsgCreateEx(const char* name, const char* recipient,
        uint8_t flags, int8_t ttl)
{
    SKALASSERT(SkalIsAsciiString(name));
    SKALASSERT(SkalIsAsciiString(recipient));
    if (ttl <= 0) {
        ttl = SKAL_DEFAULT_TTL;
    }

    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->timestamp_us = SkalPlfNow_us();
    msg->ref = 1;
    msg->version = SKAL_MSG_VERSION;
    gMsgRefCount_DEBUG++;
    msg->flags = flags;
    msg->name = SkalStrdup(name);
    if (SkalPlfThreadIsSkal()) {
        // The current thread is managed by SKAL
        msg->sender = skalMsgThreadName(SkalPlfThreadGetName());
    } else {
        // The current thread is not managed by SKAL
        msg->sender = skalMsgThreadName("skal-external");
    }
    msg->recipient = skalMsgThreadName(recipient);
    msg->ttl = ttl;
    msg->fields = CdsMapCreate(NULL, 0,
            SkalStringCompare, msg, NULL, skalFieldMapUnref);
    msg->alarms = CdsListCreate(NULL, 0, (void(*)(CdsListItem*))SkalAlarmUnref);

    return msg;
}


SkalMsg* SkalMsgCreate(const char* name, const char* recipient)
{
    return SkalMsgCreateEx(name, recipient, 0, 0);
}


void SkalMsgSetSender(SkalMsg* msg, const char* sender)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(sender));
    free(msg->sender);
    msg->sender = skalMsgThreadName(sender);
}


void SkalMsgSetIFlags(SkalMsg* msg, uint8_t iflags)
{
    SKALASSERT(msg != NULL);
    msg->iflags |= iflags;
}


void SkalMsgResetIFlags(SkalMsg* msg, uint8_t iflags)
{
    SKALASSERT(msg != NULL);
    msg->iflags &= ~iflags;
}


uint8_t SkalMsgIFlags(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->iflags;
}


void SkalMsgRef(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    gMsgRefCount_DEBUG++;
    msg->ref++;
}


void SkalMsgUnref(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    gMsgRefCount_DEBUG--;
    msg->ref--;
    if (msg->ref <= 0) {
        free(msg->name);
        free(msg->sender);
        free(msg->recipient);
        CdsMapDestroy(msg->fields);
        CdsListDestroy(msg->alarms);
        free(msg);
    }
}


int64_t SkalMsgTimestamp_us(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->timestamp_us;
}


const char* SkalMsgName(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->name;
}


const char* SkalMsgSender(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->sender;
}


const char* SkalMsgRecipient(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->recipient;
}


uint8_t SkalMsgFlags(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->flags;
}


int8_t SkalMsgTtl(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->ttl;
}


void SkalMsgDecrementTtl(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    if (msg->ttl > 0) {
        msg->ttl--;
    }
}


void SkalMsgAddInt(SkalMsg* msg, const char* name, int64_t i)
{
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_INT);
    field->i = i;
}


void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d)
{
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_DOUBLE);
    field->d = d;
}


void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s)
{
    SKALASSERT(s != NULL);
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_STRING);
    field->s = SkalStrdup(s);
    field->size_B = strlen(field->s) + 1;
}


void SkalMsgAddFormattedString(SkalMsg* msg, const char* name,
        const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_STRING);
    field->s = SkalVSPrintf(format, ap);
    field->size_B = strlen(field->s) + 1;
    va_end(ap);
}


void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const uint8_t* miniblob, int size_B)
{
    SKALASSERT(miniblob != NULL);
    SKALASSERT(size_B > 0);
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_MINIBLOB);
    field->miniblob = SkalMalloc(size_B);
    memcpy(field->miniblob, miniblob, size_B);
    field->size_B = size_B;
}


void SkalMsgAttachBlob(SkalMsg* msg, const char* name, SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    skalMsgField* field = skalMsgFieldAllocate(msg, name,
            SKAL_MSG_FIELD_TYPE_BLOB);
    field->blob = blob;
    SkalBlobRef(blob);
    SKALPANIC_MSG("Attaching blobs is not yet supported"); // TODO
}


void SkalMsgAttachAlarm(SkalMsg* msg, SkalAlarm* alarm)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(alarm != NULL);
    bool inserted = CdsListPushBack(msg->alarms, (CdsListItem*)alarm);
    SKALASSERT(inserted);
}


bool SkalMsgHasField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    return CdsMapSearch(msg->fields, (void*)name) != NULL;
}


bool SkalMsgHasIntField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    return (field != NULL) && (SKAL_MSG_FIELD_TYPE_INT == field->type);
}


bool SkalMsgHasDoubleField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    return (field != NULL) && (SKAL_MSG_FIELD_TYPE_DOUBLE == field->type);
}


bool SkalMsgHasStringField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    return (field != NULL) && (SKAL_MSG_FIELD_TYPE_STRING == field->type);
}


bool SkalMsgHasMiniblobField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    return (field != NULL) && (SKAL_MSG_FIELD_TYPE_MINIBLOB == field->type);
}


bool SkalMsgHasBlobField(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    return (field != NULL) && (SKAL_MSG_FIELD_TYPE_BLOB == field->type);
}


int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_INT == field->type);

    return field->i;
}


double SkalMsgGetDouble(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_DOUBLE == field->type);

    return field->d;
}


const char* SkalMsgGetString(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_STRING == field->type);

    return field->s;
}


const uint8_t* SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        int* size_B)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    SKALASSERT(size_B != NULL);

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_MINIBLOB == field->type);
    SKALASSERT(field->size_B > 0);
    SKALASSERT(field->miniblob != NULL);

    *size_B = field->size_B;
    return field->miniblob;
}


SkalBlob* SkalMsgGetBlob(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_BLOB == field->type);

    SkalBlob* blob = field->blob;
    SKALASSERT(blob != NULL);
    SkalBlobRef(blob);
    return blob;
}


SkalAlarm* SkalMsgDetachAlarm(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(msg->alarms != NULL);
    return (SkalAlarm*)CdsListPopFront(msg->alarms);
}


// TODO: Implement SkalMsgCopy
#if 0
SkalMsg* SkalMsgCopy(const SkalMsg* msg, bool refBlobs, const char* recipient)
{
}
#endif


char* SkalMsgToJson(const SkalMsg* msg)
{
    SkalStringBuilder* sb = SkalStringBuilderCreate(SKAL_JSON_INITIAL_CAPACITY);
    SkalStringBuilderAppend(sb,
            "{\n"
            " \"version\": %d,\n"
            " \"name\": \"%s\",\n"
            " \"sender\": \"%s\",\n"
            " \"recipient\": \"%s\",\n"
            " \"ttl\": %d,\n"
            " \"flags\": %u,\n"
            " \"iflags\": %u,\n"
            " \"fields\": [\n",
            (int)SKAL_MSG_VERSION,
            SkalMsgName(msg),
            SkalMsgSender(msg),
            SkalMsgRecipient(msg),
            (int)SkalMsgTtl(msg),
            (unsigned int)SkalMsgFlags(msg),
            (unsigned int)SkalMsgIFlags(msg));

    CdsMapIteratorReset(msg->fields, true);
    void* key;
    for (   CdsMapItem* item = CdsMapIteratorNext(msg->fields, &key);
            item != NULL;
            item = CdsMapIteratorNext(msg->fields, &key) ) {
        skalFieldToJson(sb, (const char*)key, (skalMsgField*)item);
    }

    if (!CdsMapIsEmpty(msg->fields)) {
        SkalStringBuilderTrim(sb, 2); // Remove final ",\n"
    }
    SkalStringBuilderAppend(sb, "\n ]");

    if (CdsListIsEmpty(msg->alarms)) {
        SkalStringBuilderAppend(sb, "\n}\n");
    } else {
        SkalStringBuilderAppend(sb, ",\n \"alarms\": [\n");
        CDSLIST_FOREACH(msg->alarms, SkalAlarm, alarm) {
            skalAlarmToJson(sb, alarm);
        }
        SkalStringBuilderTrim(sb, 2); // Remove final ",\n"
        SkalStringBuilderAppend(sb, "\n ]\n}\n");
    }

    return SkalStringBuilderFinish(sb);
}


SkalMsg* SkalMsgCreateFromJson(const char* json)
{
    SKALASSERT(json != NULL);

    // Create an empty but coherent message
    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->ref = 1;
    // Leave `version` at 0
    msg->fields = CdsMapCreate(NULL, 0,
            SkalStringCompare, msg, NULL, skalFieldMapUnref);
    msg->alarms = CdsListCreate(NULL, 0,
            (void(*)(CdsListItem*))SkalAlarmUnref);
    gMsgRefCount_DEBUG++;

    // Try to parse the JSON string
    if (!skalMsgParseJson(json, msg)) {
        SkalMsgUnref(msg);
        msg = NULL;
    }
    return msg;
}


void SkalSetDomain(const char* domain)
{
    SKALASSERT(SkalIsAsciiString(domain));
    free(gDomain);
    gDomain = SkalStrdup(domain);
}


const char* SkalDomain(void)
{
    return gDomain;
}


int64_t SkalMsgRefCount_DEBUG(void)
{
    return gMsgRefCount_DEBUG;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static char* skalMsgThreadName(const char* name)
{
    SKALASSERT(SkalIsAsciiString(name));
    char* ret;
    if (strchr(name, '@') != NULL) {
        ret = SkalStrdup(name);
    } else {
        ret = SkalSPrintf("%s@%s", name, gDomain);
    }
    return ret;
}


static skalMsgField* skalMsgFieldAllocate(SkalMsg* msg,
        const char* name, skalMsgFieldType type)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name));
    skalMsgField* field = SkalMallocZ(sizeof(*field));
    field->type = type;
    field->name = SkalStrdup(name);
    SKALASSERT(CdsMapInsert(msg->fields, field->name, &field->item));
    return field;
}


static void skalFieldMapUnref(CdsMapItem* item)
{
    skalMsgField* field = (skalMsgField*)item;
    switch (field->type) {
    case SKAL_MSG_FIELD_TYPE_STRING :
        free(field->s);
        break;
    case SKAL_MSG_FIELD_TYPE_MINIBLOB :
        free(field->miniblob);
        break;
    case SKAL_MSG_FIELD_TYPE_BLOB :
        SkalBlobUnref(field->blob);
        break;
    default :
        break; // nothing to do
    }
    free(field->name);
    free(field);
}


static void skalFieldToJson(SkalStringBuilder* sb,
        const char* name, const skalMsgField* field)
{
    SKALASSERT(SkalIsAsciiString(name));
    SKALASSERT(field != NULL);

    switch (field->type) {
    case SKAL_MSG_FIELD_TYPE_INT :
        SkalStringBuilderAppend(sb,
                "  {\n"
                "   \"name\": \"%s\",\n"
                "   \"type\": \"int\",\n"
                "   \"value\": %lld\n"
                "  },\n",
                name,
                (long long)field->i);
        break;

    case SKAL_MSG_FIELD_TYPE_DOUBLE :
        // *IMPORTANT* Take care how the double is converted into string such
        // that it does not lose precision. The `DECIMAL_DIG` macro exists in
        // C99 and is the number of digits in decimal representation needed to
        // get back exactly the same number, which is what we need here.
        SkalStringBuilderAppend(sb,
                "  {\n"
                "   \"name\": \"%s\",\n"
                "   \"type\": \"double\",\n"
                "   \"value\": %.*e\n"
                "  },\n",
                name,
                (int)DECIMAL_DIG,
                field->d);
        break;

    case SKAL_MSG_FIELD_TYPE_STRING :
        SkalStringBuilderAppend(sb,
                "  {\n"
                "   \"name\": \"%s\",\n"
                "   \"type\": \"string\",\n"
                "   \"value\": \"%s\"\n"
                "  },\n",
                name,
                field->s);
        break;

    case SKAL_MSG_FIELD_TYPE_MINIBLOB :
        {
            char* base64 = SkalBase64Encode(field->miniblob, field->size_B);
            SkalStringBuilderAppend(sb,
                    "  {\n"
                    "   \"name\": \"%s\",\n"
                    "   \"type\": \"miniblob\",\n"
                    "   \"value\": \"%s\"\n"
                    "  },\n",
                    name,
                    base64);
            free(base64);
        }
        break;

    case SKAL_MSG_FIELD_TYPE_BLOB :
        {
            const char* id = "";
            if (SkalBlobId(field->blob) != NULL) {
                id = SkalBlobId(field->blob);
            }
            SkalStringBuilderAppend(sb,
                    "  {\n"
                    "   \"name\": \"%s\",\n"
                    "   \"type\": \"blob\",\n"
                    "   \"value\": \"%s\"\n"
                    "  },\n",
                    name,
                    id);
        }
        break;

    default :
        SKALPANIC_MSG("Unknown message data type %d", (int)field->type);
    }
}


static void skalAlarmToJson(SkalStringBuilder* sb, SkalAlarm* alarm)
{
    SKALASSERT(alarm != NULL);
    char* json = SkalAlarmToJson(alarm, 2);
    SKALASSERT(json != NULL);
    SkalStringBuilderAppend(sb, "%s,\n", json);
    free(json);
}


static const char* skalMsgSkipSpaces(const char* str)
{
    while ((*str != '\0') && isspace(*str)) {
        str++;
    }
    return str;
}

static bool skalMsgParseJson(const char* json, SkalMsg* msg)
{
    // Find starting '{'
    json = skalMsgSkipSpaces(json);
    if (*json != '{') {
        SkalLog("SkalMsg: Invalid JSON: Expected '{'");
        return false;
    }
    json++;
    json = skalMsgSkipSpaces(json);

    // Parse JSON object properties one after the other
    while ((*json != '\0') && (*json != '}')) {
        char* str;
        json = skalMsgParseJsonString(json, &str);
        if (NULL == json) {
            return false;
        }

        json = skalMsgSkipSpaces(json);
        if (*json != ':') {
            SkalLog("SkalMsg: Invalid JSON: Expected ':'");
            free(str);
            return false;
        }
        json++;
        json = skalMsgSkipSpaces(json);

        json = skalMsgParseJsonProperty(json, str, msg);
        free(str);
        if (NULL == json) {
            return false;
        }
        json = skalMsgSkipSpaces(json);
    } // For each JSON property

    // Check all properties are there
    if (msg->version != SKAL_MSG_VERSION) {
        SkalLog("SkalMsg: Invalid JSON: 'version' is required");
        return false;
    }
    if ('\0' == msg->name[0]) {
        SkalLog("SkalMsg: Invalid JSON: 'name' is required");
        return false;
    }
    if ('\0' == msg->sender[0]) {
        SkalLog("SkalMsg: Invalid JSON: 'sender' is required");
        return false;
    }
    if ('\0' == msg->recipient[0]) {
        SkalLog("SkalMsg: Invalid JSON: 'recipient' is required");
        return false;
    }
    if (msg->ttl <= 0) {
        SkalLog("SkalMsg: Invalid JSON: 'ttl' is required");
        return false;
    }

    return true;
}


static const char* skalMsgParseJsonProperty(const char* json,
        const char* name, SkalMsg* msg)
{
    if (strcmp(name, "version") == 0) {
        int tmp;
        if (sscanf(json, "%d", &tmp) != 1) {
            SkalLog("SkalMsg: Invalid JSON: Can't parse integer for 'version'");
            return NULL;
        }
        if (tmp != SKAL_MSG_VERSION) {
            SkalLog("SkalMsg: Invalid JSON: Wrong version %d, expected %d",
                    msg->version, SKAL_MSG_VERSION);
            return NULL;
        }
        msg->version = SKAL_MSG_VERSION;
        // Skip version number
        while ((*json != '\0') && isdigit(*json)) {
            json++;
        }

    } else if (strcmp(name, "name") == 0) {
        json = skalMsgParseJsonString(json, &msg->name);

    } else if (strcmp(name, "sender") == 0) {
        json = skalMsgParseJsonString(json, &msg->sender);

    } else if (strcmp(name, "recipient") == 0) {
        json = skalMsgParseJsonString(json, &msg->recipient);

    } else if (strcmp(name, "ttl") == 0) {
        int tmp;
        if (sscanf(json, "%d", &tmp) != 1) {
            SkalLog("SkalMsg: Invalid JSON: Can't parse integer for 'ttl'");
            return NULL;
        }
        if ((tmp <= 0) || (tmp > 127)) {
            SkalLog("SkalMsg: Invalid JSON: 'ttl' must be >0 and <=127");
            return NULL;
        }
        msg->ttl = (int8_t)tmp;
        // Skip TTL
        while ((*json != '\0') && isdigit(*json)) {
            json++;
        }

    } else if (strcmp(name, "flags") == 0) {
        unsigned int tmp;
        if (sscanf(json, "%u", &tmp) != 1) {
            SkalLog("SkalMsg: Invalid JSON: Can't parse unsigned integer for 'flags'");
            return NULL;
        }
        msg->flags = tmp;
        // Skip flags
        while ((*json != '\0') && isdigit(*json)) {
            json++;
        }

    } else if (strcmp(name, "iflags") == 0) {
        unsigned int tmp;
        if (sscanf(json, "%u", &tmp) != 1) {
            SkalLog("SkalMsg: Invalid JSON: Can't parse unsigned integer for 'iflags'");
            return NULL;
        }
        msg->iflags = tmp;
        // Skip flags
        while ((*json != '\0') && isdigit(*json)) {
            json++;
        }

    } else if (strcmp(name, "fields") == 0) {
        if (*json != '[') {
            SkalLog("SkalMsg: Invalid JSON: Expected '['");
            return NULL;
        }
        json++;
        json = skalMsgSkipSpaces(json);
        while ((*json != '\0') && (*json != ']')) {
            // Allocate an empty be coherent field
            skalMsgField* field = SkalMallocZ(sizeof(*field));
            field->type = SKAL_MSG_FIELD_TYPE_INT;
            json = skalMsgParseJsonField(json, field);
            if (NULL == json) {
                skalFieldMapUnref((CdsMapItem*)field);
                return NULL;
            }
            bool inserted = CdsMapInsert(msg->fields,
                        field->name, &field->item);
            SKALASSERT(inserted);
            if (*json != '\0') {
                json = skalMsgSkipSpaces(json);
            }
        }
        if (*json != ']') {
            SkalLog("SkalMsg: Invalid JSON: Expected ']'");
            return NULL;
        }
        json++;

    } else if (strcmp(name, "alarms") == 0) {
        if (*json != '[') {
            SkalLog("SkalMsg: Invalid JSON: Expected '['");
            return NULL;
        }
        json++;
        json = skalMsgSkipSpaces(json);
        while ((*json != '\0') && (*json != ']')) {
            SkalAlarm* alarm = SkalAlarmCreateFromJson(&json);
            if (NULL == alarm) {
                return NULL;
            }
            SkalMsgAttachAlarm(msg, alarm);
            json = skalMsgSkipSpaces(json);
            if (',' == *json) {
                json++;
                json = skalMsgSkipSpaces(json);
            }
        }
        if (*json != ']') {
            SkalLog("SkalMsg: Invalid JSON: Expected ']'");
            return NULL;
        }
        json++;

    } else {
        // Unknown property
        SkalLog("SkalMsg: Invalid JSON: Unknown property '%s'",
                name);
        return NULL;
    }

    if (json != NULL) {
        json = skalMsgSkipSpaces(json);
        if (',' == *json) {
            json++;
        }
    }
    return json;
}


static const char* skalMsgParseJsonField(const char* json, skalMsgField* field)
{
    if (*json != '{') {
        SkalLog("SkalMsg: Invalid JSON: Expected '{'");
        return NULL;
    }
    json++;
    json = skalMsgSkipSpaces(json);

    field->type = SKAL_MSG_FIELD_TYPE_NOTHING;
    const char* value = NULL;
    while ((*json != '\0') && (*json != '}')) {
        // Parse property name
        char* str;
        json = skalMsgParseJsonString(json, &str);
        if (NULL == json) {
            return NULL;
        }
        skalMsgFieldProperty property = skalMsgFieldStrToProp(str);
        if (SKAL_MSG_FIELD_PROPERTY_INVALID == property) {
            SkalLog("SkalMsg: Invalid JSON: Unknown field property '%s'", str);
            free(str);
            return NULL;
        }
        free(str);

        json = skalMsgSkipSpaces(json);
        if (*json != ':') {
            SkalLog("SkalMsg: Invalid JSON: Expected ':'");
            return NULL;
        }
        json++;
        json = skalMsgSkipSpaces(json);

        // Parse property value
        switch (property) {
        case SKAL_MSG_FIELD_PROPERTY_NAME :
            json = skalMsgParseJsonString(json, &field->name);
            break;

        case SKAL_MSG_FIELD_PROPERTY_TYPE :
            json = skalMsgParseJsonString(json, &str);
            if (json != NULL) {
                if (strcmp(str, "int") == 0) {
                    field->type = SKAL_MSG_FIELD_TYPE_INT;
                } else if (strcmp(str, "double") == 0) {
                    field->type = SKAL_MSG_FIELD_TYPE_DOUBLE;
                } else if (strcmp(str, "string") == 0) {
                    field->type = SKAL_MSG_FIELD_TYPE_STRING;
                } else if (strcmp(str, "miniblob") == 0) {
                    field->type = SKAL_MSG_FIELD_TYPE_MINIBLOB;
                } else if (strcmp(str, "blob") == 0) {
                    field->type = SKAL_MSG_FIELD_TYPE_BLOB;
                } else {
                    SkalLog("SkalMsg: Invalid JSON: Unknown field type '%s'",
                            str);
                    json = NULL;
                }
                free(str);
            }
            break;

        case SKAL_MSG_FIELD_PROPERTY_VALUE :
            // We will parse the field value later because we don't necessarily
            // know its type yet (happens if "value" is defined before "type" in
            // the JSON text). We memorise the start of the value string and
            // then skip it.
            value = json;
            if (*json == '"') {
                // The value is a string
                json++;
                while ((*json != '\0') && (*json != '"')) {
                    if ('\\' == *json) {
                        json++;
                        if (*json != '\0') {
                            json++;
                        }
                    } else {
                        json++;
                    }
                }
                if ('\0' == *json) {
                    SkalLog("SkalMsg: Invalid JSON: Unexpected end of string");
                    return NULL;
                }
                SKALASSERT('"' == *json);
                json++;

            } else {
                // Just skip until the comma or end of object
                while ((*json != '\0') && (*json != ',') && (*json != '}')) {
                    json++;
                }
                if ('\0' == *json) {
                    SkalLog("SkalMsg: Invalid JSON: Unexpected end of string");
                    return NULL;
                }
            }
            break;

        default :
            SKALPANIC_MSG("Internal bug! Fix me!");
        }

        if (NULL == json) {
            return NULL;
        }

        json = skalMsgSkipSpaces(json);
        if (',' == *json) {
            json++;
            json = skalMsgSkipSpaces(json);
        }
    }

    if ('\0' == *json) {
        SkalLog("SkalMsg: Invalid JSON: expected '}'");
        return NULL;
    }
    SKALASSERT('}' == *json);
    json++;
    json = skalMsgSkipSpaces(json);
    if (',' == *json) {
        json++;
    }

    // Check that we have all 3 properties necessary to define a field
    if ('\0' == field->name[0]) {
        SkalLog("SkalMsg: Invalid JSON: Field requires 'name'");
        return NULL;
    }
    if (SKAL_MSG_FIELD_TYPE_NOTHING == field->type) {
        SkalLog("SkalMsg: Invalid JSON: Field requires 'type'");
        return NULL;
    }
    if (NULL == value) {
        SkalLog("SkalMsg: Invalid JSON: Field requires 'value'");
        return NULL;
    }

    // Now we can parse the value
    switch (field->type) {
    case SKAL_MSG_FIELD_TYPE_INT :
        {
            long long tmp;
            if (sscanf(value, "%lld", &tmp) != 1) {
                SkalLog("SkalMsg: Invalid JSON: Can't parse integer value");
                return NULL;
            }
            field->i = tmp;
        }
        break;

    case SKAL_MSG_FIELD_TYPE_DOUBLE :
        if (sscanf(value, "%le", &field->d) != 1) {
            SkalLog("SkalMsg: Invalid JSON: Can't parse double value");
            return NULL;
        }
        break;

    case SKAL_MSG_FIELD_TYPE_STRING :
        {
            const char* tmp = skalMsgParseJsonString(value, &field->s);
            if (NULL == tmp) {
                return NULL;
            }
            field->size_B = strlen(field->s) + 1;
        }
        break;

    case SKAL_MSG_FIELD_TYPE_MINIBLOB :
        {
            char* b64;
            const char* tmp = skalMsgParseJsonString(value, &b64);
            if (NULL == tmp) {
                free(b64);
                return NULL;
            }
            field->miniblob = SkalBase64Decode(b64, &field->size_B);
            free(b64);
            if (NULL == field->miniblob) {
                SkalLog("SkalMsg: Invalid JSON: Failed to base64-decode miniblob");
                return NULL;
            }
        }
        break;

    case SKAL_MSG_FIELD_TYPE_BLOB :
        SKALPANIC_MSG("Blobs not yet supported");
        break;

    default :
        SKALPANIC_MSG("Unknown field type: %d", (int)(field->type));
        break;
    }

    return json;
}


static const char* skalMsgParseJsonString(const char* json, char** str)
{
    SKALASSERT(str != NULL);
    *str = NULL;

    json = skalMsgSkipSpaces(json);
    if (*json != '"') {
        SkalLog("SkalMsg: Invalid JSON: Expected '\"' character");
        return NULL;
    }
    json++;

    SkalStringBuilder* sb = SkalStringBuilderCreate(1024);
    while ((*json != '\0') && (*json != '"')) {
        if ('\\' == *json) {
            json++;
            if ('\0' == *json) {
                SkalLog("SkalMsg: Invalid JSON: null character after \\");
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
        SkalLog("SkalMsg: Invalid JSON: Unterminated string");
        free(s);
        return NULL;
    }
    SKALASSERT('"' == *json);
    json++;

    *str = s;
    return json;
}


static skalMsgFieldProperty skalMsgFieldStrToProp(const char* str)
{
    SKALASSERT(str != NULL);
    skalMsgFieldProperty property = SKAL_MSG_FIELD_PROPERTY_INVALID;
    if (strcmp(str, "name") == 0) {
        property = SKAL_MSG_FIELD_PROPERTY_NAME;
    } else if (strcmp(str, "type") == 0) {
        property = SKAL_MSG_FIELD_PROPERTY_TYPE;
    } else if (strcmp(str, "value") == 0) {
        property = SKAL_MSG_FIELD_PROPERTY_VALUE;
    }
    return property;
}
