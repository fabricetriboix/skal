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

#include "skal-thread.h"
#include "skal-msg.h"
#include "skal-queue.h"
#include "cdsmap.h"
#include <stdlib.h>
#include <string.h>



/*----------------+
 | Macros & Types |
 +----------------*/


/** Structure that represents an item of the `SkalThread.xoff` map
 *
 * NB: We do not count references because we know by design it's referenced only
 * once at creation.
 */
typedef struct
{
    CdsMapItem item;

    /** Name of the recipient that got its queue full because of me */
    char origin[SKAL_NAME_MAX];

    /** Last time we sent a 'skal-ntf-xon' message to `origin` */
    int64_t lastNtfXonTime_us;
} skalXoffItem;


/** Structure that represents an item of the `SkalThread.ntfXon` map
 *
 * NB: We do not count references because we know by design it's referenced only
 * once at creation.
 */
typedef struct
{
    CdsMapItem item;

    /** Name of the thread to notify */
    char origin[SKAL_NAME_MAX];
} skalNtfXonItem;


/** Structure that defines a thread
 *
 * Please note that except when first created and after termination, this
 * structure is access only by the thread itself.
 *
 * NB: We do not count references because we know by design it's referenced only
 * once at creation.
 */
struct SkalThread
{
    CdsMapItem item;

    /** Thread configuration */
    SkalThreadCfg cfg;

    /** Message queue for this thread */
    SkalQueue* queue;

    /** The actual thread */
    SkalPlfThread* thread;
};


/** Thread private stuff
 *
 * Each thread will allocate one such structure and will store its pointer as
 * thread-specific data.
 */
typedef struct
{
    /** Back pointer to the thread structure
     *
     * This structure is guaranteed to exist and not be modified (except for its
     * `queue`) for the lifetime of the thread.
     */
    SkalThread* thread;

    /** Map of threads that sent me an xoff msg - made of `skalXoffItem` */
    CdsMap* xoff;

    /** Map of threads to notify when they can start sending again
     *
     * This map is made of `skalNtfXonItem`.
     */
    CdsMap* ntfXon;
} skalThreadPrivate;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Private function to send a message; allow failure if recipient not found
 *
 * You will lose ownership of the message (unless `failToMaster` is `false` and
 * the recipient is not found, in which case no action is taken and you retain
 * ownership of the message).
 *
 * The default behaviour if the recipient is not found is to assume it belongs
 * to another process. So the message is sent to the master thread who will
 * route it appropriately.
 *
 * @param msg          [in,out] Message to send
 * @param failToMaster [in]     If the recipient is not found, forward the
 *                              message to `skal-master` for routing
 *
 * @return If `failToMaster` is `true`, this function always returns `true`;
 *         if `failToMaster` is `false`, this function returns `true` if the
 *         recipient was found, and `false` otherwise
 */
static bool skalMsgSendPriv(SkalMsg* msg, bool failToMaster);


/** Create a thread
 *
 * @param cfg [in] Description of thread to create; must not be NULL and must
 *                 comply with the restrictions indicated in `skal.h`
 *
 * @return The newly created thread; this function never returns NULL
 */
static SkalThread* skalThreadCreatePriv(const SkalThreadCfg* cfg);


/** Function to unreference a thread structure
 *
 * Please note this function will not terminate or cancel the thread. This
 * function blocks until the thread has terminated.
 */
static void skalThreadUnref(SkalThread* thread);


/** Function to unreference a map item where the item just needs to be freed */
static void skalMapUnrefFree(CdsMapItem* item);


/** Run a SKAL thread
 *
 * @param arg [in] Argument; actually a `SkalThread*` structure representing
 *                 this very thread
 */
static void skalThreadRun(void* arg);


/** Utility function for a thread: handle internal message
 *
 * @param priv [in,out] Thread private data
 * @param msg  [in]     Received message
 *
 * @return `false` to stop this thread now, otherwise `true`
 */
static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg);


/** Utility function for a thread: send "xon" messages to all blocked threads
 *
 * @param priv [in,out] Thread private data
 */
static void skalThreadSendXon(skalThreadPrivate* priv);


/** Utility function for a thread: retry "ntf-xon" messages
 *
 * For all threads we are blocked on and have timed out, we re-send a "ntf-xon"
 * to the blocking thread to tell it to unblock us.
 *
 * @param priv   [in,out] Thread private data
 * @param now_us [in]     Current time, in us
 */
static void skalThreadRetryNtfXon(skalThreadPrivate* priv, int64_t now_us);


/** Message processing function for the master thread
 *
 * @param cookie [in]     Not used
 * @param msg    [in,out] Received message
 *
 * @return `false` to terminate the thread, otherwise `true`
 */
static bool skalMasterProcessMsg(void* cookie, SkalMsg* msg);



/*------------------+
 | Global variables |
 +------------------*/


/** Mutex to protect the `gThreads` map */
static SkalPlfMutex* gMutex = NULL;


/** Map of threads
 *
 * Items are of type `SkalThread`. Keys are of type `const char*` and are the
 * name of the threads, i.e. `SkalThread->cfg.name`.
 */
static CdsMap* gThreads = NULL;


/** Are we in the process of terminating this process and all its threads? */
static bool gTerminating = false;


/** Master thread
 *
 * This is not in the `gThreads` map because of its special role.
 */
static SkalThread* gMaster = NULL;


/** Global queue
 *
 * This global queue is used to communicate between the original thread (i.e.
 * the one from the `main()` function) and the master thread.
 *
 * In practice, it's used only in the end to indicate that all threads have
 * terminated.
 */
static SkalQueue* gGlobalQueue = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalThreadInit(void)
{
    SKALASSERT(NULL == gMutex);
    SKALASSERT(NULL == gThreads);
    SKALASSERT(NULL == gMaster);
    SKALASSERT(NULL == gGlobalQueue);

    gTerminating = false;

    gMutex = SkalPlfMutexCreate();

    char threadName[SKAL_NAME_MAX];
    SkalPlfThreadGetName(threadName, sizeof(threadName));
    char name[SKAL_NAME_MAX];
    snprintf(name, sizeof(name), "%s-queue", threadName);
    gGlobalQueue = SkalQueueCreate(name, SKAL_THREADS_MAX);

    snprintf(name, sizeof(name), "%s-threads", threadName);
    gThreads = CdsMapCreate(name,              // name
                            SKAL_THREADS_MAX,  // capacity
                            SkalStringCompare, // compare
                            NULL,              // cookie
                            NULL,              // keyUnref
                            (void(*)(CdsMapItem*))skalThreadUnref); // itemUnref

    // Create the master thread
    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    strncpy(cfg.name, "skal-master", sizeof(cfg.name) - 1);
    cfg.processMsg = skalMasterProcessMsg;
    cfg.queueThreshold = SKAL_MSG_LIST_MAX;
    gMaster = skalThreadCreatePriv(&cfg);
}


void SkalThreadExit(void)
{
    // Tell the master thread to terminate, it will trigger and handle the
    // termination of all threads in this process.

    gTerminating = true;
    SKALASSERT(gMaster != NULL);
    SkalMsg* msg = SkalMsgCreate("skal-master-terminate",
            "skal-master", 0, NULL);
    SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gMaster->queue, msg);

    SkalMsg* resp = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strcmp(SkalMsgSender(resp), "skal-master") == 0);
    SKALASSERT(strcmp(SkalMsgType(resp), "skal-terminated") == 0);

    skalThreadUnref(gMaster);
    SKALASSERT(CdsMapIsEmpty(gThreads)); // All threads must have terminated now
    gMaster = NULL;

    // Release all the other global variables

    CdsMapDestroy(gThreads);
    gThreads = NULL;

    SkalQueueDestroy(gGlobalQueue);
    gGlobalQueue = NULL;

    SkalPlfMutexDestroy(gMutex);
    gMutex = NULL;
}


void SkalThreadCreate(const SkalThreadCfg* cfg)
{
    if (!gTerminating) {
        SkalThread* thread = skalThreadCreatePriv(cfg);
        SkalPlfMutexLock(gMutex);
        SKALASSERT(CdsMapSearch(gThreads, (void*)(thread->cfg.name)) == NULL);
        bool inserted = CdsMapInsert(gThreads,
                (void*)(thread->cfg.name), &thread->item);
        SKALASSERT(inserted);
        SkalPlfMutexUnlock(gMutex);
    }
}


void SkalMsgSend(SkalMsg* msg)
{
    bool sent = skalMsgSendPriv(msg, true);
    SKALASSERT(sent);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static bool skalMsgSendPriv(SkalMsg* msg, bool failToMaster)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(gMaster != NULL);

    SkalPlfMutexLock(gMutex);

    SkalThread* recipient = (SkalThread*)CdsMapSearch(gThreads,
            (void*)SkalMsgRecipient(msg));
    // NB: If the recipient is not found (and thus possibly in another process
    // or machine), forward the message to `skal-master` which will route it.
    if ((NULL == recipient) && failToMaster) {
        recipient = gMaster;
    }

    if (recipient != NULL) {
        SkalQueuePush(recipient->queue, msg);
        if (    SkalQueueIsOverHighThreshold(recipient->queue)
             && !(SkalMsgInternalFlags(msg) & SKAL_MSG_IFLAG_INTERNAL)) {
            // Recipient queue is full and we are not sending an internal msg
            //  => Enter XOFF mode by sending an xoff msg to myself
            skalThreadPrivate* priv = SkalPlfThreadGetSpecific();
            SKALASSERT(priv != NULL);
            SKALASSERT(priv->thread != NULL);
            SkalMsg* msg3 = SkalMsgCreate("skal-xoff",
                    priv->thread->cfg.name, 0, NULL);
            SkalMsgSetInternalFlags(msg3, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(msg3, "origin", SkalMsgRecipient(msg));
            SkalQueuePush(priv->thread->queue, msg3);
        }
        // else: Message successfully sent => Nothing else to do
    }

    SkalPlfMutexUnlock(gMutex);

    return (recipient != NULL);
}


static SkalThread* skalThreadCreatePriv(const SkalThreadCfg* cfg)
{
    SKALASSERT(cfg != NULL);
    SKALASSERT(SkalIsAsciiString(cfg->name, SKAL_NAME_MAX));
    SKALASSERT(strlen(cfg->name) > 0);
    SKALASSERT(cfg->processMsg != NULL);

    SkalThread* thread = SkalMallocZ(sizeof(*thread));
    thread->cfg = *cfg;
    if (thread->cfg.queueThreshold <= 0) {
        thread->cfg.queueThreshold = SKAL_DEFAULT_QUEUE_THRESHOLD;
    }
    if (thread->cfg.xoffTimeout_us <= 0) {
        thread->cfg.xoffTimeout_us = SKAL_DEFAULT_XOFF_TIMEOUT_us;
    }

    char name[SKAL_NAME_MAX];
    snprintf(name, sizeof(name), "%s-queue", thread->cfg.name);
    thread->queue = SkalQueueCreate(name, thread->cfg.queueThreshold);

    // NB: Create the actual thread last, as it may access the structure
    thread->thread = SkalPlfThreadCreate(thread->cfg.name,
            skalThreadRun, thread);

    return thread;
}


static void skalThreadUnref(SkalThread* thread)
{
    SKALASSERT(thread != NULL);
    SkalPlfThreadJoin(thread->thread);
    SkalQueueDestroy(thread->queue);
    free(thread);
}


static void skalMapUnrefFree(CdsMapItem* item)
{
    free(item);
}


static void skalThreadRun(void* arg)
{
    SKALASSERT(arg != NULL);
    SkalThread* thread = (SkalThread*)arg;
    SkalPlfThreadSetName(thread->cfg.name);

    skalThreadPrivate* priv = SkalMallocZ(sizeof(*priv));
    priv->thread = thread;
    bool isMasterThread = (strcmp(thread->cfg.name, "skal-master") == 0);

    char name[SKAL_NAME_MAX];
    snprintf(name, sizeof(name), "%s-xoff", thread->cfg.name);
    priv->xoff = CdsMapCreate(name,              // name
                              SKAL_XOFF_MAX,     // capacity
                              SkalStringCompare, // compare
                              NULL,              // cookie
                              NULL,              // keyUnref
                              skalMapUnrefFree); // itemUnref

    snprintf(name, sizeof(name), "%s-ntf-xon", thread->cfg.name);
    priv->ntfXon = CdsMapCreate(name,              // name
                                SKAL_XOFF_MAX,     // capacity
                                SkalStringCompare, // compare
                                NULL,              // cookie
                                NULL,              // keyUnref
                                skalMapUnrefFree); // itemUnref

    SkalPlfThreadSetSpecific(priv);

    bool stop = false;
    while (!stop) {
        bool inXoff = false;
        if (!CdsMapIsEmpty(priv->xoff)) {
            // I have some `xoff` pending, so I am blocked
            if (!isMasterThread) {
                // NB: The master thread should never block
                inXoff = true;
            }
        }
        // TODO: Add timer to retry sending `skal-ntf-xon` messages
        SkalMsg* msg = SkalQueuePop_BLOCKING(thread->queue, inXoff);

        if (SkalMsgInternalFlags(msg) & SKAL_MSG_IFLAG_INTERNAL) {
            if (!skalThreadHandleInternalMsg(priv, msg)) {
                stop = true;
            }
        }

        if (    !CdsMapIsEmpty(priv->ntfXon)
             && !SkalQueueIsOverLowThreshold(thread->queue) ) {
            // Some threads are waiting for my queue not to be full anymore
            skalThreadSendXon(priv);
        }

        int64_t start_ns = SkalPlfNow_ns();
        if (!thread->cfg.processMsg(thread->cfg.cookie, msg)) {
            stop = true;
        }
        int64_t duration_ns = SkalPlfNow_ns() - start_ns;
        (void)duration_ns; // TODO: Do something with `duration_ns`

        skalThreadRetryNtfXon(priv, start_ns / 1000LL);

        SkalMsgUnref(msg);
    } // Thread loop

    // This thread is now terminated
    if (isMasterThread) {
        // I am the master thread: unblock the global queue now
        SkalMsg* msg = SkalMsgCreate("skal-terminated", "skal-main", 0, NULL);
        SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        SkalQueuePush(gGlobalQueue, msg);

    } else {
        // I am not the master thread: tell the master thread I'm finished
        skalThreadSendXon(priv); // free up any threads blocked on me
        SkalMsg* msg = SkalMsgCreate("skal-terminated", "skal-master", 0, NULL);
        SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        SkalQueuePush(gMaster->queue, msg);
    }
}


static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg)
{
    SKALASSERT(priv != NULL);

    bool ok = true;

    const char* type = SkalMsgType(msg);
    if (strcmp(type, "skal-xoff") == 0) {
        // A thread is telling me to stop sending to it
        const char* origin = SkalMsgGetString(msg, "origin");
        skalXoffItem* xoffItem = (skalXoffItem*)CdsMapSearch(priv->xoff,
                (void*)origin);
        if (NULL == xoffItem) {
            xoffItem = SkalMallocZ(sizeof(*xoffItem));
            strncpy(xoffItem->origin, origin, sizeof(xoffItem->origin) - 1);
            bool inserted = CdsMapInsert(priv->xoff,
                    (void*)xoffItem->origin, &xoffItem->item);
            SKALASSERT(inserted);
        }

        // Tell originating thread to notify me when I can send again
        SkalMsg* msg2 = SkalMsgCreate("skal-ntf-xon", origin, 0, NULL);
        SkalMsgAddString(msg2, "origin", priv->thread->cfg.name);
        SkalMsgSetInternalFlags(msg2, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgSend(msg2);

    } else if (strcmp(type, "skal-xon") == 0) {
        // A thread is telling me I can resume sending to it
        const char* sender = SkalMsgGetString(msg, "origin");
        skalXoffItem* xoffItem = (skalXoffItem*)CdsMapSearch(priv->xoff,
                (void*)sender);
        if (NULL == xoffItem) {
            // Received an unexpected `xon` message; this can happen in case of
            // retries, just ignore it
        } else {
            CdsMapItemRemove(priv->xoff, &xoffItem->item);
        }

    } else if (strcmp(type, "skal-ntf-xon") == 0) {
        // A thread is telling me I should notify it when it can send messages
        // again to me
        const char* origin = SkalMsgGetString(msg, "origin");
        skalNtfXonItem* ntfXonItem = (skalNtfXonItem*)CdsMapSearch(
                priv->ntfXon, (void*)origin);
        if (NULL == ntfXonItem) {
            ntfXonItem = SkalMallocZ(sizeof(*ntfXonItem));
            strncpy(ntfXonItem->origin, origin, sizeof(ntfXonItem->origin) - 1);
            bool inserted = CdsMapInsert(priv->ntfXon,
                    (void*)ntfXonItem->origin, &ntfXonItem->item);
            SKALASSERT(inserted);
        }

    } else if (strcmp(type, "skal-terminate") == 0) {
        ok = false;
    }

    return ok;
}


static void skalThreadSendXon(skalThreadPrivate* priv)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(priv->thread != NULL);

    CdsMapIteratorReset(priv->ntfXon, true);
    for (skalNtfXonItem* ntfXonItem = (skalNtfXonItem*)CdsMapIteratorNext(
                priv->ntfXon, NULL);
            ntfXonItem != NULL;
            ntfXonItem = (skalNtfXonItem*)CdsMapIteratorNext(priv->ntfXon, NULL) ) {
        SkalMsg* msg = SkalMsgCreate("skal-xon", ntfXonItem->origin, 0, NULL);
        SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(msg, "origin", priv->thread->cfg.name);
        SkalMsgSend(msg);
    }

    CdsMapClear(priv->ntfXon);
}


static void skalThreadRetryNtfXon(skalThreadPrivate* priv, int64_t now_us)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(priv->xoff != NULL);

    CdsMapIteratorReset(priv->xoff, true);
    for (skalXoffItem* xoffItem = (skalXoffItem*)CdsMapIteratorNext(
                priv->xoff, NULL);
            xoffItem != NULL;
            xoffItem = (skalXoffItem*)CdsMapIteratorNext(priv->xoff, NULL)) {
        int64_t elapsed_us = now_us - xoffItem->lastNtfXonTime_us;
        if (elapsed_us > priv->thread->cfg.xoffTimeout_us) {
            SkalMsg* msg = SkalMsgCreate("skal-ntf-xon",
                    xoffItem->origin, 0, NULL);
            SkalMsgAddString(msg, "origin", priv->thread->cfg.name);
            SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgSend(msg);
            xoffItem->lastNtfXonTime_us = now_us;
        }
    }
}


static bool skalMasterProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgRecipient(msg), "skal-master") != 0) {
        // This message is not for the master thread
        //  => Forward it
        bool sent = skalMsgSendPriv(msg, false);
        if (!sent) {
            // The message's recipient is not in this process
            //  => TODO: send to skald
            return false;
        }
        return true;
    }

    bool ok = true;
    const char* type = SkalMsgType(msg);
    if (strcmp(type, "skal-master-terminate") == 0) {
        // I have been asked to terminate myself
        //  => Tell all threads to terminate themselves
        SkalPlfMutexLock(gMutex);
        if (CdsMapIsEmpty(gThreads)) {
            ok = false;
        } else {
            CdsMapIteratorReset(gThreads, true);
            for (   CdsMapItem* item = CdsMapIteratorNext(gThreads, NULL);
                    item != NULL;
                    item = CdsMapIteratorNext(gThreads, NULL) ) {
                SkalThread* thread = (SkalThread*)item;
                SkalMsg* msg = SkalMsgCreate("skal-terminate",
                        thread->cfg.name, 0, NULL);
                SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
                SkalQueuePush(thread->queue, msg);
            } // for each thread in this process
        }
        SkalPlfMutexUnlock(gMutex);

    } else if (strcmp(type, "skal-terminated") == 0) {
        // A thread is telling me it just finished
        SkalPlfMutexLock(gMutex);
        CdsMapRemove(gThreads, (void*)SkalMsgSender(msg));
        if (CdsMapIsEmpty(gThreads)) {
            ok = false;
        }
        SkalPlfMutexUnlock(gMutex);
    }

    return ok;
}
