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
#include "skal-blob.h"
#include "cdslist.h"
#include "cdsmap.h"
#include <stdlib.h>
#include <string.h>
#include <float.h>



/*----------------+
 | Macros & Types |
 +----------------*/


/** Capacity increment of a JSON string: 16KiB */
#define SKAL_JSON_INITIAL_CAPACITY (16 * 1024)


typedef enum {
    SKAL_MSG_FIELD_TYPE_INT,
    SKAL_MSG_FIELD_TYPE_DOUBLE,
    SKAL_MSG_FIELD_TYPE_STRING,
    SKAL_MSG_FIELD_TYPE_MINIBLOB,
    SKAL_MSG_FIELD_TYPE_BLOB
} skalMsgFieldType;


typedef struct {
    CdsMapItem       item;
    int8_t           ref;
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


struct SkalMsg
{
    CdsListItem item; // SKAL messages can be enqueued and dequeued
    int8_t      ref;
    uint8_t     flags;
    uint8_t     internalFlags;
    char        type[SKAL_NAME_MAX];
    char        sender[SKAL_NAME_MAX];
    char        recipient[SKAL_NAME_MAX];
    char        marker[SKAL_NAME_MAX];
    CdsMap*     fields; // Map of `skalMsgField`, indexed by field name
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


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



/*------------------+
 | Global variables |
 +------------------*/


/** Message counter; use to make unique message markers */
static uint64_t gMsgCounter = 0;


/** Number of message references in this process */
static int64_t gMsgRefCount_DEBUG = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalMsg* SkalMsgCreate(const char* type, const char* recipient,
        uint8_t flags, const char* marker)
{
    SKALASSERT(SkalIsAsciiString(type, SKAL_NAME_MAX));
    SKALASSERT(SkalIsAsciiString(recipient, SKAL_NAME_MAX));
    if (marker != NULL) {
        SKALASSERT(SkalIsAsciiString(marker, SKAL_NAME_MAX));
    }

    // FIXME: potential race condition on gMsgCounter... fix that.
    unsigned long long n = ++gMsgCounter;
    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->ref = 1;
    gMsgRefCount_DEBUG++;
    msg->flags = flags;
    strncpy(msg->type, type, sizeof(msg->type) - 1);
    if (SkalPlfThreadGetSpecific() != NULL) {
        // The current thread is managed by SKAL
        SkalPlfThreadGetName(msg->sender, sizeof(msg->sender));
    } else {
        // The current thread is not managed by SKAL
        strncpy(msg->sender, "skal-external", sizeof(msg->sender) - 1);
    }
    strncpy(msg->recipient, recipient, sizeof(msg->recipient) - 1);
    if (marker != NULL) {
        strncpy(msg->marker, marker, sizeof(msg->marker) - 1);
    } else {
        snprintf(msg->marker, sizeof(msg->marker), "%llu", n);
    }
    msg->fields = CdsMapCreate(NULL, SKAL_FIELDS_MAX,
            SkalStringCompare, msg, NULL, skalFieldMapUnref);

    return msg;
}


void SkalMsgSetInternalFlags(SkalMsg* msg, uint8_t flags)
{
    SKALASSERT(msg != NULL);
    msg->internalFlags |= flags;
}


void SkalMsgResetInternalFlags(SkalMsg* msg, uint8_t flags)
{
    SKALASSERT(msg != NULL);
    msg->internalFlags &= ~flags;
}


uint8_t SkalMsgInternalFlags(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->internalFlags;
}


void SkalMsgRef(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref++;

    // Reference attached blobs, if any
    CdsMapIteratorReset(msg->fields, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(msg->fields, NULL);
            item != NULL;
            item = CdsMapIteratorNext(msg->fields, NULL) ) {
        skalMsgField* field = (skalMsgField*)item;
        if (SKAL_MSG_FIELD_TYPE_BLOB == field->type) {
            SkalBlobRef(field->blob);
        }
    }

    gMsgRefCount_DEBUG++;
}


void SkalMsgUnref(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref--;

    // Unreference attached blobs, if any
    CdsMapIteratorReset(msg->fields, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(msg->fields, NULL);
            item != NULL;
            item = CdsMapIteratorNext(msg->fields, NULL) ) {
        skalMsgField* field = (skalMsgField*)item;
        if (SKAL_MSG_FIELD_TYPE_BLOB == field->type) {
            SkalBlobUnref(field->blob);
        }
    }

    gMsgRefCount_DEBUG--;
    if (msg->ref <= 0) {
        CdsMapDestroy(msg->fields);
        free(msg);
    }
}


const char* SkalMsgType(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->type;
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


const char* SkalMsgMarker(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->marker;
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


SkalBlob* SkalMsgDetachBlob(SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgField* field = (skalMsgField*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(field != NULL);
    SKALASSERT(SKAL_MSG_FIELD_TYPE_BLOB == field->type);

    SkalBlob* blob = field->blob;
    SKALASSERT(blob != NULL);
    SkalBlobRef(blob);
    CdsMapItemRemove(msg->fields, &field->item);
    return blob;
}


// TODO
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
            " \"type\": \"%s\",\n"
            " \"sender\": \"%s\",\n"
            " \"recipient\": \"%s\",\n"
            " \"marker\": \"%s\",\n"
            " \"flags\": %u,\n"
            " \"iflags\": %u,\n"
            " \"fields\": [\n",
            (int)SKAL_MSG_VERSION,
            SkalMsgType(msg),
            SkalMsgSender(msg),
            SkalMsgRecipient(msg),
            SkalMsgMarker(msg),
            (unsigned int)SkalMsgFlags(msg),
            (unsigned int)SkalMsgInternalFlags(msg));

    CdsMapIteratorReset(msg->fields, true);
    void* key;
    for (   CdsMapItem* item = CdsMapIteratorNext(msg->fields, &key);
            item != NULL;
            item = CdsMapIteratorNext(msg->fields, &key) ) {
        skalFieldToJson(sb, (const char*)key, (skalMsgField*)item);
    }

    SkalStringBuilderTrim(sb, 2);
    SkalStringBuilderAppend(sb, "\n ]\n}\n");

    return SkalStringBuilderFinish(sb);
}


SkalMsg* SkalMsgCreateFromJson(const char* json)
{
    // TODO: from here
    return NULL;
}


int64_t SkalMsgRefCount_DEBUG(void)
{
    return gMsgRefCount_DEBUG;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static skalMsgField* skalAllocMsgField(SkalMsg* msg,
        const char* name, skalMsgFieldType type)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    skalMsgField* field = SkalMallocZ(sizeof(*field));
    field->ref = 1;
    field->type = type;
    strncpy(field->name, name, sizeof(field->name) - 1);
    SKALASSERT(CdsMapInsert(msg->fields, field->name, &field->item));
    return field;
}


static void skalFieldMapUnref(CdsMapItem* item)
{
    skalMsgField* field = (skalMsgField*)item;
    field->ref--;
    if (field->ref <= 0) {
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
