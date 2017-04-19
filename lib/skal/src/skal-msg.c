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
    char             name[SKAL_NAME_MAX];
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
    char        name[SKAL_NAME_MAX];
    char        sender[SKAL_NAME_MAX];
    char        recipient[SKAL_NAME_MAX];
    CdsMap*     fields; // Map of `skalMsgField`, indexed by field name
    CdsList*    alarms; // List of `SkalAlarm`
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Set a thread name, appending the local domain if no domain is specified */
static void skalSetThreadName(char* buffer, int size_B, const char* t);


/** Allocate a message field and add it to the `msg`
 *
 * @param msg  [in,out] Message the field will be added to
 * @param name [in]     Field name
 * @param type [in]     Field type
 *
 * @return The newly created field; this function never returns NULL
 */
static skalMsgField* skalAllocMsgField(SkalMsg* msg,
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
 * @param json   [in]  JSON text to parse; must not be NULL
 * @param buffer [out] Parse JSON string
 * @param size_B [in]  Size of `buffer`, in characters
 *
 * @return Pointer to the JSON string after the closing double-quote, or NULL if
 *         error or if the JSON string is too big for the supplied buffer
 */
static const char* skalMsgParseJsonString(const char* json,
        char* buffer, int size_B);



/*------------------+
 | Global variables |
 +------------------*/


/** Domain name */
static char gDomain[SKAL_DOMAIN_NAME_MAX] = "^INVAL^";


/** Number of message references in this process */
static int64_t gMsgRefCount_DEBUG = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalMsg* SkalMsgCreateEx(const char* name, const char* recipient,
        uint8_t flags, int8_t ttl)
{
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    SKALASSERT(SkalIsAsciiString(recipient, SKAL_NAME_MAX));
    if (ttl <= 0) {
        ttl = SKAL_DEFAULT_TTL;
    }

    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->timestamp_us = SkalPlfNow_us();
    msg->ref = 1;
    msg->version = SKAL_MSG_VERSION;
    gMsgRefCount_DEBUG++;
    msg->flags = flags;
    strncpy(msg->name, name, sizeof(msg->name) - 1);
    if (SkalPlfThreadIsSkal()) {
        // The current thread is managed by SKAL
        const char* name = SkalPlfThreadGetName();
        skalSetThreadName(msg->sender, sizeof(msg->sender), name);
    } else {
        // The current thread is not managed by SKAL
        skalSetThreadName(msg->sender, sizeof(msg->sender), "skal-external");
    }
    skalSetThreadName(msg->recipient, sizeof(msg->recipient), recipient);
    msg->ttl = ttl;
    msg->fields = CdsMapCreate(NULL, SKAL_FIELDS_MAX,
            SkalStringCompare, msg, NULL, skalFieldMapUnref);
    msg->alarms = CdsListCreate(NULL, SKAL_FIELDS_MAX,
            (void(*)(CdsListItem*))SkalAlarmUnref);

    return msg;
}


SkalMsg* SkalMsgCreate(const char* name, const char* recipient)
{
    return SkalMsgCreateEx(name, recipient, 0, 0);
}


void SkalMsgSetSender(SkalMsg* msg, const char* sender)
{
    SKALASSERT(msg != NULL);
    skalSetThreadName(msg->sender, sizeof(msg->sender), sender);
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
        CdsListDestroy(msg->alarms);
        CdsMapDestroy(msg->fields);
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
    skalMsgField* field = skalAllocMsgField(msg, name, SKAL_MSG_FIELD_TYPE_INT);
    field->i = i;
}


void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d)
{
    skalMsgField* field = skalAllocMsgField(msg, name,
            SKAL_MSG_FIELD_TYPE_DOUBLE);
    field->d = d;
}


void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s)
{
    SKALASSERT(s != NULL);
    skalMsgField* field = skalAllocMsgField(msg, name,
            SKAL_MSG_FIELD_TYPE_STRING);
    field->size_B = strlen(s) + 1;
    field->s = SkalMallocZ(field->size_B);
    memcpy(field->s, s, field->size_B);
}


void SkalMsgAddFormattedString(SkalMsg* msg, const char* name,
        const char* format, ...)
{
    va_list ap;
    va_start(ap, format);
    char* s = SkalVSPrintf(format, ap);
    va_end(ap);
    SkalMsgAddString(msg, name, s);
    free(s);
}


void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const uint8_t* miniblob, int size_B)
{
    SKALASSERT(miniblob != NULL);
    SKALASSERT(size_B > 0);
    skalMsgField* field = skalAllocMsgField(msg, name,
            SKAL_MSG_FIELD_TYPE_MINIBLOB);
    field->miniblob = SkalMalloc(size_B);
    memcpy(field->miniblob, miniblob, size_B);
    field->size_B = size_B;
}


void SkalMsgAttachBlob(SkalMsg* msg, const char* name, SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    skalMsgField* field = skalAllocMsgField(msg, name,
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
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    return CdsMapSearch(msg->fields, (void*)name) != NULL;
}


int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_INT == field->type);

    return field->i;
}


double SkalMsgGetDouble(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_DOUBLE == field->type);

    return field->d;
}


const char* SkalMsgGetString(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_STRING == field->type);

    return field->s;
}


const uint8_t* SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        int* size_B)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
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
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

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
    msg->fields = CdsMapCreate(NULL, SKAL_FIELDS_MAX,
            SkalStringCompare, msg, NULL, skalFieldMapUnref);
    msg->alarms = CdsListCreate(NULL, SKAL_FIELDS_MAX,
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
    SKALASSERT(SkalIsAsciiString(domain, sizeof(gDomain)));
    snprintf(gDomain, sizeof(gDomain), "%s", domain);
}


const char* SkalDomain(void)
{
    if ('\0' == gDomain[0]) {
        return NULL;
    }
    return gDomain;
}


int64_t SkalMsgRefCount_DEBUG(void)
{
    return gMsgRefCount_DEBUG;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skalSetThreadName(char* buffer, int size_B, const char* t)
{
    SKALASSERT(buffer != NULL);
    SKALASSERT(size_B > 0);
    SKALASSERT(SkalIsAsciiString(t, SKAL_NAME_MAX));
    if (strchr(t, '@') != NULL) {
        snprintf(buffer, size_B, "%s", t);
    } else {
        int n = snprintf(buffer, size_B, "%s@%s", t, gDomain);
        SKALASSERT(n < size_B);
    }
}


static skalMsgField* skalAllocMsgField(SkalMsg* msg,
        const char* name, skalMsgFieldType type)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    skalMsgField* field = SkalMallocZ(sizeof(*field));
    field->type = type;
    strncpy(field->name, name, sizeof(field->name) - 1);
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
    free(field);
}


static void skalFieldToJson(SkalStringBuilder* sb,
        const char* name, const skalMsgField* field)
{
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
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
        // C99 and is the number of digits needed in decimal representation
        // needed to get back exactly the same number, which is what we need
        // here.
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
        SkalStringBuilderAppend(sb,
                "  {\n"
                "   \"name\": \"%s\",\n"
                "   \"type\": \"blob\",\n"
                "   \"value\": \"%s\"\n"
                "  },\n",
                name,
                SkalBlobId(field->blob));
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
    char name[SKAL_NAME_MAX];
    while ((*json != '\0') && (*json != '}')) {
        json = skalMsgParseJsonString(json, name, sizeof(name));
        if (NULL == json) {
            return false;
        }

        json = skalMsgSkipSpaces(json);
        if (*json != ':') {
            SkalLog("SkalMsg: Invalid JSON: Expected ':'");
            return false;
        }
        json++;
        json = skalMsgSkipSpaces(json);

        json = skalMsgParseJsonProperty(json, name, msg);
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
        json = skalMsgParseJsonString(json, msg->name, sizeof(msg->name));

    } else if (strcmp(name, "sender") == 0) {
        json = skalMsgParseJsonString(json, msg->sender, sizeof(msg->sender));

    } else if (strcmp(name, "recipient") == 0) {
        json = skalMsgParseJsonString(json,
                msg->recipient, sizeof(msg->recipient));

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
    char buffer[SKAL_NAME_MAX];
    const char* value = NULL;
    int length = 0; // used only if the value is a string
    while ((*json != '\0') && (*json != '}')) {
        // Parse property name
        json = skalMsgParseJsonString(json, buffer, sizeof(buffer));
        if (NULL == json) {
            return NULL;
        }
        json = skalMsgSkipSpaces(json);
        if (*json != ':') {
            SkalLog("SkalMsg: Invalid JSON: Expected ':'");
            return NULL;
        }
        json++;
        json = skalMsgSkipSpaces(json);

        // Parse property value
        if (strcmp(buffer, "name") == 0) {
            json = skalMsgParseJsonString(json,
                    field->name, sizeof(field->name));
            if (NULL == json) {
                return NULL;
            }

        } else if (strcmp(buffer, "type") == 0) {
            json = skalMsgParseJsonString(json, buffer, sizeof(buffer));
            if (NULL == json) {
                return NULL;
            }
            if (strcmp(buffer, "int") == 0) {
                field->type = SKAL_MSG_FIELD_TYPE_INT;
            } else if (strcmp(buffer, "double") == 0) {
                field->type = SKAL_MSG_FIELD_TYPE_DOUBLE;
            } else if (strcmp(buffer, "string") == 0) {
                field->type = SKAL_MSG_FIELD_TYPE_STRING;
            } else if (strcmp(buffer, "miniblob") == 0) {
                field->type = SKAL_MSG_FIELD_TYPE_MINIBLOB;
            } else if (strcmp(buffer, "blob") == 0) {
                field->type = SKAL_MSG_FIELD_TYPE_BLOB;
            } else {
                SkalLog("SkalMsg: Invalid JSON: Unknown field type '%s'",
                        buffer);
                return NULL;
            }

        } else if (strcmp(buffer, "value") == 0) {
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
                length = json - value; // Includes backslashes, but that's OK
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

        } else {
            SkalLog("SkalMsg: Invalid JSON: Unknown field property '%s'",
                    buffer);
            return NULL; // Unknown property name
        }

        json = skalMsgSkipSpaces(json);
        if (',' == *json) {
            json++;
        }
        json = skalMsgSkipSpaces(json);
    }

    if ('\0' == *json) {
        SkalLog("SkalMsg: Invalid JSON: Unexpected end of string");
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
            field->size_B = length + 1;
            field->s = SkalMalloc(field->size_B);
            const char* tmp = skalMsgParseJsonString(value,
                    field->s, field->size_B);
            if (NULL == tmp) {
                return NULL;
            }
        }
        break;

    case SKAL_MSG_FIELD_TYPE_MINIBLOB :
        {
            char* b64 = SkalMalloc(length + 1);
            const char* tmp = skalMsgParseJsonString(value, b64, length + 1);
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


static const char* skalMsgParseJsonString(const char* json,
        char* buffer, int size_B)
{
    json = skalMsgSkipSpaces(json);
    if (*json != '"') {
        SkalLog("SkalMsg: Invalid JSON: Expected '\"' character");
        return NULL;
    }
    json++;

    int count = 0;
    while ((*json != '\0') && (*json != '"')) {
        if ('\\' == *json) {
            json++;
            if ('\0' == *json) {
                SkalLog("SkalMsg: Invalid JSON: null character after \\");
                return NULL;
            }
        }
        buffer[count] = *json;
        count++;
        json++;
        if ((count >= size_B) && (*json != '"')) {
            SkalLog("SkalMsg: Invalid JSON: String too long (expected max %d chars)",
                    size_B);
            return NULL;
        }
    }

    if ('\0' == *json) {
        SkalLog("SkalMsg: Invalid JSON: Unterminated string");
        return NULL;
    }
    SKALASSERT('"' == *json);
    json++;

    buffer[count] = '\0';
    return json;
}
