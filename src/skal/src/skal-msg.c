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
#include "cdsmap.h"
#include <stdlib.h>
#include <string.h>



/*----------------+
 | Macros & Types |
 +----------------*/


typedef enum {
    SKAL_MSG_DATA_TYPE_INT,
    SKAL_MSG_DATA_TYPE_DOUBLE,
    SKAL_MSG_DATA_TYPE_STRING,
    SKAL_MSG_DATA_TYPE_MINIBLOB,
    SKAL_MSG_DATA_TYPE_BLOB
} skalMsgDataType;


typedef struct {
    CdsMapItem      item;
    int8_t          ref;
    skalMsgDataType type;
    SkalMsg*        msg; // backpointer
    int             size_B;
    char            name[SKAL_NAME_MAX];
    union {
        int64_t   i;
        double    d;
        char*     s;
        void*     miniblob;
        SkalBlob* blob;
    };
} skalMsgData;


struct SkalMsg
{
    int8_t  ref;
    uint8_t flags;
    char    type[SKAL_NAME_MAX];
    char    marker[SKAL_NAME_MAX];
    CdsMap* fields;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


static skalMsgData* skalAllocMsgData(SkalMsg* msg,
        const char* name, skalMsgDataType type);

static int skalFieldMapCompare(void* leftkey, void* rightkey, void* cookie);

static void skalFieldMapUnref(CdsMapItem* litem);



/*------------------+
 | Global variables |
 +------------------*/


static uint64_t gMsgCounter = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


SkalMsg* SkalMsgCreate(const char* type, uint8_t flags, const char* marker)
{
    SKALASSERT(SkalIsAsciiString(type, SKAL_NAME_MAX));
    if (marker != NULL) {
        SKALASSERT(SkalIsAsciiString(marker, SKAL_NAME_MAX));
    }

    unsigned long long n = ++gMsgCounter;
    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->ref = 1;
    msg->flags = flags;
    strncpy(msg->type, type, sizeof(msg->type) - 1);
    if (marker != NULL) {
        strncpy(msg->marker, marker, sizeof(msg->marker) - 1);
    } else {
        snprintf(msg->marker, sizeof(msg->marker), "%llu", n);
    }
    msg->fields = CdsMapCreate(NULL, SKAL_MAX_FIELDS,
            skalFieldMapCompare, msg, NULL, skalFieldMapUnref);

    return msg;
}


void SkalMsgRef(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref++;
}


void SkalMsgUnref(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref--;
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


const char* SkalMsgMarker(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    return msg->marker;
}


void SkalMsgAddInt(SkalMsg* msg, const char* name, int64_t i)
{
    skalMsgData* data = skalAllocMsgData(msg, name, SKAL_MSG_DATA_TYPE_INT);
    data->i = i;
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
}


void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d)
{
    skalMsgData* data = skalAllocMsgData(msg, name, SKAL_MSG_DATA_TYPE_DOUBLE);
    data->d = d;
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
}


void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s)
{
    SKALASSERT(s != NULL);
    skalMsgData* data = skalAllocMsgData(msg, name, SKAL_MSG_DATA_TYPE_STRING);
    data->s = strdup(s);
    SKALASSERT(data->s != NULL);
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
}


// TODO
#if 0
void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const void* data, int size_B)
{
}
#endif


void SkalMsgAttachBlob(SkalMsg* msg, const char* name, SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    skalMsgData* data = skalAllocMsgData(msg, name, SKAL_MSG_DATA_TYPE_BLOB);
    data->blob = blob;
    SkalBlobRef(blob);
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
}


int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_INT == data->type);

    return data->i;
}


double SkalMsgGetDouble(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_DOUBLE == data->type);

    return data->d;
}


const char* SkalMsgGetString(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_STRING == data->type);

    return data->s;
}


// TODO
#if 0
int SkalMsgGetMiniBlob(const SkalMsg* msg, const char* name,
        void* buffer, int size_B)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    SKALASSERT(buffer != NULL);
    SKALASSERT(size_B > 0);

}
#endif


SkalBlob* SkalMsgGetBlob(const SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_BLOB == data->type);

    SkalBlob* blob = data->blob;
    SKALASSERT(blob != NULL);
    SkalBlobRef(blob);
    return blob;
}


SkalBlob* SkalMsgDetachBlob(SkalMsg* msg, const char* name)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_BLOB == data->type);

    SkalBlob* blob = data->blob;
    SKALASSERT(blob != NULL);
    SkalBlobRef(blob);
    CdsMapItemRemove(msg->fields, &data->item);
    return blob;
}


// TODO
#if 0
SkalMsg* SkalMsgCopy(const SkalMsg* msg, bool refBlobs)
{
}
#endif



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static skalMsgData* skalAllocMsgData(SkalMsg* msg,
        const char* name, skalMsgDataType type)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));

    skalMsgData* data = SkalMallocZ(sizeof(*data));
    data->ref = 1;
    data->type = type;
    data->msg = msg;
    strncpy(data->name, name, sizeof(data->name) - 1);

    return data;
}


static int skalFieldMapCompare(void* leftkey, void* rightkey, void* cookie)
{
    return strcmp((const char*)leftkey, (const char*)rightkey);
}


static void skalFieldMapUnref(CdsMapItem* item)
{
    skalMsgData* data = (skalMsgData*)item;
    data->ref--;
    if (data->ref <= 0) {
        switch (data->type) {
        case SKAL_MSG_DATA_TYPE_STRING :
            free(data->s);
            break;
        case SKAL_MSG_DATA_TYPE_MINIBLOB :
            free(data->miniblob);
            break;
        case SKAL_MSG_DATA_TYPE_BLOB :
            SkalBlobUnref(data->blob);
            break;
        default :
            break; // nothing to do
        }
        free(data);
    }
}
