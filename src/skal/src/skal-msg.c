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
        void*     miniblob;
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
    CdsMap*     fields;
};


struct SkalQueue
{
    char            name[SKAL_NAME_MAX];
    SkalPlfMutex*   mutex;
    SkalPlfCondVar* condvar;
    bool            shutdown;
    int64_t         threshold;
    int64_t         assertThreshold;
    CdsList*        internal;
    CdsList*        urgent;
    CdsList*        regular;
};


struct SkalMsgList
{
    CdsList* list; // list of outgoing messages
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

    // TODO: potential race conditon... fix that.
    unsigned long long n = ++gMsgCounter;
    SkalMsg* msg = SkalMallocZ(sizeof(*msg));
    msg->ref = 1;
    gMsgRefCount_DEBUG++;
    msg->flags = flags;
    strncpy(msg->type, type, sizeof(msg->type) - 1);
    SkalPlfGetCurrentThreadName(msg->sender, sizeof(msg->sender));
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


void SkalMsgRef(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref++;
    gMsgRefCount_DEBUG++;
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


void SkalMsgUnref(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    msg->ref--;
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
    queue->shutdown = false;
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


void SkalQueueShutdown(SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    queue->shutdown = true;
}


bool SkalQueueIsInShutdownMode(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    return queue->shutdown;
}


void SkalQueueDestroy(SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(queue->shutdown);
    SkalPlfMutexLock(queue->mutex);
    CdsListDestroy(queue->regular);
    CdsListDestroy(queue->urgent);
    CdsListDestroy(queue->internal);
    SkalPlfMutexUnlock(queue->mutex);
    SkalPlfCondVarDestroy(queue->condvar);
    SkalPlfMutexDestroy(queue->mutex);
    free(queue);
}


int SkalQueuePush(SkalQueue* queue, SkalMsg* msg)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(msg != NULL);

    SkalPlfMutexLock(queue->mutex);

    int ret = 0;
    bool isInternal = msg->internalFlags & SKAL_MSG_IFLAG_INTERNAL;
    bool isShutdown = queue->shutdown;
    if (isInternal) {
        isShutdown = false; // Always push internal messages
    }

    if (isShutdown) {
        ret = -1;
    } else {
        bool pushed;
        if (isInternal) {
            pushed = CdsListPushBack(queue->internal, &msg->item);
        } else if (msg->flags & SKAL_MSG_FLAG_URGENT) {
            pushed = CdsListPushBack(queue->urgent, &msg->item);
        } else {
            pushed = CdsListPushBack(queue->regular, &msg->item);
        }
        SKALASSERT(pushed);
        SkalPlfCondVarSignal(queue->condvar);

        // Check if the queue is full, but not if we pushed an internal msg
        if (!isInternal) {
            int64_t size = CdsListSize(queue->urgent)
                + CdsListSize(queue->regular);
            SKALASSERT(size < queue->assertThreshold);
            if (size >= queue->threshold) {
                ret = 1;
            }
        }
    }

    SkalPlfMutexUnlock(queue->mutex);

    return ret;
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


// XXX
#if 0
SkalMsg* SkalQueuePeek(const SkalQueue* queue)
{
    SKALASSERT(queue);

    SkalPlfMutexLock(queue->mutex);
    SkalMsg* msg;
    if (!CdsListIsEmpty(queue->urgent)) {
        msg = (SkalMsg*)CdsListFront(queue->urgent);
    } else {
        msg = (SkalMsg*)CdsListFront(queue->regular);
    }
    SkalPlfMutexUnlock(queue->mutex);

    return msg;
}
#endif


SkalMsgList* SkalMsgListCreate(void)
{
    SkalMsgList* msgList = SkalMallocZ(sizeof(*msgList));
    msgList->list = CdsListCreate(NULL, SKAL_MSG_LIST_MAX,
            (void(*)(CdsListItem*))SkalMsgUnref);
    return msgList;
}


void SkalMsgListDestroy(SkalMsgList* msgList)
{
    SKALASSERT(msgList != NULL);
    CdsListDestroy(msgList->list);
    free(msgList);
}


void SkalMsgListAdd(SkalMsgList* msgList, SkalMsg* msg)
{
    SKALASSERT(msgList != NULL);
    SKALASSERT(msg != NULL);

    bool pushed = false;
    if (    (msg->internalFlags & SKAL_MSG_IFLAG_INTERNAL)
         || (msg->flags & SKAL_MSG_FLAG_URGENT) ) {
        pushed = CdsListPushFront(msgList->list, &msg->item);
    } else {
        pushed = CdsListPushBack(msgList->list, &msg->item);
    }
    SKALASSERT(pushed);
}


SkalMsg* SkalMsgListPop(SkalMsgList* msgList)
{
    SKALASSERT(msgList != NULL);
    return (SkalMsg*)CdsListPopFront(msgList->list);
}


bool SkalMsgListIsEmpty(const SkalMsgList* msgList)
{
    SKALASSERT(msgList != NULL);
    return CdsListIsEmpty(msgList->list);
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
