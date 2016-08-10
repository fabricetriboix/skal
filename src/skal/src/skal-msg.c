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
        uint8_t*  miniblob;
        SkalBlob* blob;
    };
} skalMsgData;


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
    CdsMap*     fields; // Map of `skalMsgData`, indexed by field name
};


struct SkalQueue
{
    char            name[SKAL_NAME_MAX];
    SkalPlfMutex*   mutex;
    SkalPlfCondVar* condvar;
    int64_t         threshold;
    int64_t         assertThreshold;
    CdsList*        internal;
    CdsList*        urgent;
    CdsList*        regular;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Allocate a message data item (aka a field)
 *
 * \param msg  [in,out] Message the field will be added to
 * \param name [in]     Field name
 * \param type [in]     Field type
 *
 * \return The newly created datum; this function never returns NULL
 */
static skalMsgData* skalAllocMsgData(SkalMsg* msg,
        const char* name, skalMsgDataType type);


/** Function to unreference a field in a message field map */
static void skalFieldMapUnref(CdsMapItem* item);



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
    CdsMapIterator* iter = CdsMapIteratorCreate(msg->fields, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(iter, NULL);
            item != NULL;
            item = CdsMapIteratorNext(iter, NULL) ) {
        skalMsgData* data = (skalMsgData*)item;
        if (SKAL_MSG_DATA_TYPE_BLOB == data->type) {
            SkalBlobRef(data->blob);
        }
    }
    CdsMapIteratorDestroy(iter);

    gMsgRefCount_DEBUG++;
}


void SkalMsgUnref(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref--;

    // Unreference attached blobs, if any
    CdsMapIterator* iter = CdsMapIteratorCreate(msg->fields, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(iter, NULL);
            item != NULL;
            item = CdsMapIteratorNext(iter, NULL) ) {
        skalMsgData* data = (skalMsgData*)item;
        if (SKAL_MSG_DATA_TYPE_BLOB == data->type) {
            SkalBlobUnref(data->blob);
        }
    }
    CdsMapIteratorDestroy(iter);

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


void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const uint8_t* miniblob, int size_B)
{
    SKALASSERT(miniblob != NULL);
    SKALASSERT(size_B > 0);
    skalMsgData* data = skalAllocMsgData(msg, name,
            SKAL_MSG_DATA_TYPE_MINIBLOB);
    data->miniblob = SkalMalloc(size_B);
    memcpy(data->miniblob, miniblob, size_B);
    data->size_B = size_B;
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
}


void SkalMsgAttachBlob(SkalMsg* msg, const char* name, SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    skalMsgData* data = skalAllocMsgData(msg, name, SKAL_MSG_DATA_TYPE_BLOB);
    data->blob = blob;
    SkalBlobRef(blob);
    SKALASSERT(CdsMapInsert(msg->fields, data->name, &data->item));
    SKALPANIC_MSG("Attaching blobs is not yet supported"); // TODO
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


const uint8_t* SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        int* size_B)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    SKALASSERT(size_B != NULL);

    skalMsgData* data = (skalMsgData*)CdsMapSearch(msg->fields, (void*)name);
    SKALASSERT(data != NULL);
    SKALASSERT(SKAL_MSG_DATA_TYPE_MINIBLOB == data->type);
    SKALASSERT(data->size_B > 0);
    SKALASSERT(data->miniblob != NULL);

    *size_B = data->size_B;
    return data->miniblob;
}


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
SkalMsg* SkalMsgCopy(const SkalMsg* msg, bool refBlobs, const char* recipient)
{
}
#endif


SkalQueue* SkalQueueCreate(const char* name, int64_t threshold)
{
    SKALASSERT(SkalIsAsciiString(name, SKAL_NAME_MAX));
    SKALASSERT(threshold > 0);

    SkalQueue* queue = SkalMallocZ(sizeof(*queue));
    strncpy(queue->name, name, SKAL_NAME_MAX - 1);
    queue->mutex = SkalPlfMutexCreate();
    queue->condvar = SkalPlfCondVarCreate();
    queue->threshold = threshold;
    if (threshold < 100) {
        queue->assertThreshold = 1000;
    } else {
        queue->assertThreshold = threshold * 10;
    }
    queue->internal=CdsListCreate(NULL, 0, (void(*)(CdsListItem*))SkalMsgUnref);
    queue->urgent  =CdsListCreate(NULL, 0, (void(*)(CdsListItem*))SkalMsgUnref);
    queue->regular =CdsListCreate(NULL, 0, (void(*)(CdsListItem*))SkalMsgUnref);

    return queue;
}


const char* SkalQueueName(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    return queue->name;
}


void SkalQueueDestroy(SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SkalPlfMutexLock(queue->mutex);
    CdsListDestroy(queue->regular);
    CdsListDestroy(queue->urgent);
    CdsListDestroy(queue->internal);
    SkalPlfMutexUnlock(queue->mutex);
    SkalPlfCondVarDestroy(queue->condvar);
    SkalPlfMutexDestroy(queue->mutex);
    free(queue);
}


void SkalQueuePush(SkalQueue* queue, SkalMsg* msg)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(msg != NULL);

    SkalPlfMutexLock(queue->mutex);
    bool pushed;
    if (msg->internalFlags & SKAL_MSG_IFLAG_INTERNAL) {
        pushed = CdsListPushBack(queue->internal, &msg->item);
    } else if (msg->flags & SKAL_MSG_FLAG_URGENT) {
        pushed = CdsListPushBack(queue->urgent, &msg->item);
    } else {
        pushed = CdsListPushBack(queue->regular, &msg->item);
    }
    SKALASSERT(pushed);
    SkalPlfCondVarSignal(queue->condvar);
    SkalPlfMutexUnlock(queue->mutex);
}


SkalMsg* SkalQueuePop_BLOCKING(SkalQueue* queue, bool internalOnly)
{
    SKALASSERT(queue != NULL);

    SkalPlfMutexLock(queue->mutex);
    if (internalOnly) {
        while (CdsListIsEmpty(queue->internal)) {
            SkalPlfCondVarWait(queue->condvar, queue->mutex);
        }
    } else {
        while (    CdsListIsEmpty(queue->internal)
                && CdsListIsEmpty(queue->urgent)
                && CdsListIsEmpty(queue->regular) ) {
            SkalPlfCondVarWait(queue->condvar, queue->mutex);
        }
    }

    SkalMsg* msg = NULL;
    if (!CdsListIsEmpty(queue->internal)) {
        msg = (SkalMsg*)CdsListPopFront(queue->internal);
    } else if (!CdsListIsEmpty(queue->urgent)) {
        msg = (SkalMsg*)CdsListPopFront(queue->urgent);
    } else {
        SKALASSERT(!CdsListIsEmpty(queue->regular));
        msg = (SkalMsg*)CdsListPopFront(queue->regular);
    }
    SKALASSERT(msg != NULL);
    SkalPlfMutexUnlock(queue->mutex);

    return msg;
}


bool SkalQueueIsFull(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(queue->threshold > 0);

    SkalPlfMutexLock(queue->mutex);
    int64_t size = CdsListSize(queue->urgent) + CdsListSize(queue->regular);
    bool isFull = (size >= queue->threshold);
    SkalPlfMutexUnlock(queue->mutex);

    return isFull;
}


bool SkalQueueIsHalfFull(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(queue->threshold > 0);

    SkalPlfMutexLock(queue->mutex);
    int64_t size = CdsListSize(queue->urgent) + CdsListSize(queue->regular);
    bool isHalfFull = (size >= (queue->threshold / 2));
    SkalPlfMutexUnlock(queue->mutex);

    return isHalfFull;
}


int64_t SkalMsgRefCount_DEBUG(void)
{
    return gMsgRefCount_DEBUG;
}



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
