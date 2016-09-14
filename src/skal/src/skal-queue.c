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

#include "skal-queue.h"
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



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


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
    if (SkalMsgInternalFlags(msg) & SKAL_MSG_IFLAG_INTERNAL) {
        pushed = CdsListPushBack(queue->internal, (CdsListItem*)msg);
    } else if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_URGENT) {
        pushed = CdsListPushBack(queue->urgent, (CdsListItem*)msg);
    } else {
        pushed = CdsListPushBack(queue->regular, (CdsListItem*)msg);
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


SkalMsg* SkalQueuePop(SkalQueue* queue, bool internalOnly)
{
    SKALASSERT(queue != NULL);

    SkalPlfMutexLock(queue->mutex);
    SkalMsg* msg = NULL;
    if (!CdsListIsEmpty(queue->internal)) {
        msg = (SkalMsg*)CdsListPopFront(queue->internal);
    } else if (!CdsListIsEmpty(queue->urgent)) {
        msg = (SkalMsg*)CdsListPopFront(queue->urgent);
    } else {
        msg = (SkalMsg*)CdsListPopFront(queue->regular);
    }
    SkalPlfMutexUnlock(queue->mutex);

    return msg;
}


bool SkalQueueIsFullOrMore(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(queue->threshold > 0);

    SkalPlfMutexLock(queue->mutex);
    int64_t size = CdsListSize(queue->urgent) + CdsListSize(queue->regular);
    bool isFull = (size >= queue->threshold);
    SkalPlfMutexUnlock(queue->mutex);

    return isFull;
}


bool SkalQueueIsHalfFullOrMore(const SkalQueue* queue)
{
    SKALASSERT(queue != NULL);
    SKALASSERT(queue->threshold > 0);

    SkalPlfMutexLock(queue->mutex);
    int64_t size = CdsListSize(queue->urgent) + CdsListSize(queue->regular);
    bool isHalfFull = (size >= (queue->threshold / 2));
    SkalPlfMutexUnlock(queue->mutex);

    return isHalfFull;
}
