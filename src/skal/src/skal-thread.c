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

    /** Count how many xoff messages I received from `origin` against how many
     * xon messages.
     */
    int count;
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

    /** Count how many "skal-ntf-xon" messages I received from `origin` against
     * how many "xon" messages I sent to it.
     */
    int count;
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
     * This structure is guaranteed to exist and not modified (except for the
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


/** Create a thread
 *
 * \param cfg [in] Description of thread to create; must not be NULL and must
 *                 comply with the restrictions indicated in `skal.h`
 *
 * \return The newly created thread; this function never returns NULL
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
 * \param arg [in] Argument; actually a `SkalThread*` structure representing
 *                 this very thread
 */
static void skalThreadRun(void* arg);


/** Utility function for a thread: handle internal message
 *
 * \param priv [in,out] Thread private data
 * \param msg  [in]     Received message
 *
 * \return `false` to stop this thread now, otherwise `true`
 */
static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg);


/** Utility function for a thread: send "xon" messages to all blocked threads
 *
 * \param priv [in,out] Thread private data
 */
static void skalThreadSendXon(skalThreadPrivate* priv);


/** Message processing function for the master thread
 *
 * \param cookie [in]     Not used
 * \param msg    [in,out] Received message
 *
 * \return `false` to terminate the thread, otherwise `true`
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
    cfg.queueThreshold = SKAL_THREADS_MAX * SKAL_MSG_LIST_MAX;
    gMaster = skalThreadCreatePriv(&cfg);
}


void SkalThreadExit(void)
{
    // Tell the master thread to terminate, it will trigger and handle the
    // termination of all threads in this process.

    gTerminating = true;
    SKALASSERT(gMaster != NULL);
    SkalMsg* msg = SkalMsgCreate("skal-terminate", "skal-master", 0, NULL);
    SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    int ret = SkalQueuePush(gMaster->queue, msg);
    SKALASSERT(0 == ret);

    SkalMsg* resp = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strncmp(SkalMsgSender(resp), "skal-master", SKAL_NAME_MAX) == 0);
    SKALASSERT(strncmp(SkalMsgType(resp), "skal-terminated", SKAL_NAME_MAX)
            == 0);

    skalThreadUnref(gMaster);
    SKALASSERT(CdsMapIsEmpty(gThreads)); // All threads must have terminated now
    gMaster = NULL;

    // Release all the other global variables

    CdsMapDestroy(gThreads);
    gThreads = NULL;

    SkalQueueShutdown(gGlobalQueue);
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
    SKALASSERT(msg != NULL);
    SKALASSERT(gMaster != NULL);

    SkalPlfMutexLock(gMutex);

    SkalThread* recipient = NULL;
    if (strncmp(SkalMsgRecipient(msg), "skal-master", SKAL_NAME_MAX) != 0) {
        // NB: No need to search the thread map for "skal-master"
        recipient = (SkalThread*)CdsMapSearch(gThreads,
                (void*)SkalMsgRecipient(msg));
    }
    if (NULL == recipient) {
        recipient = gMaster;
    }

    int ret = SkalQueuePush(recipient->queue, msg);
    if (ret < 0) {
        // The recipient queue is in shutdown mode
        //  => Drop the msg and inform the sender (ie this thread) if requested
        if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP) {
            skalThreadPrivate* priv = SkalPlfThreadGetSpecific();
            if (NULL == priv) {
                // TODO: The sender is not managed by SKAL and can't be notified
                //  => Decide what to do in such a case
            } else {
                SKALASSERT(priv->thread != NULL);
                SkalMsg* msg2 = SkalMsgCreate("skal-msg-drop",
                        priv->thread->cfg.name, 0, NULL);
                SkalMsgSetInternalFlags(msg2, SKAL_MSG_IFLAG_INTERNAL);
                SkalMsgAddString(msg2, "original-marker", SkalMsgMarker(msg));
                SkalMsgAddString(msg2, "reason", "queue-shutdown");
                int ret = SkalQueuePush(priv->thread->queue, msg2);
                SKALASSERT(0 == ret);
            }
        }
        SkalMsgUnref(msg);

    } else if (ret > 0) {
        // Message successfully pushed, but queue is full
        //  => Enter XOFF mode by sending myself an xoff msg
        skalThreadPrivate* priv = SkalPlfThreadGetSpecific();
        SKALASSERT(priv != NULL);
        SKALASSERT(priv->thread != NULL);
        SkalMsg* msg3 = SkalMsgCreate("skal-xoff",
                priv->thread->cfg.name, 0, NULL);
        SkalMsgSetInternalFlags(msg3, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(msg3, "origin", SkalMsgRecipient(msg));
        int ret3 = SkalQueuePush(priv->thread->queue, msg3);
        SKALASSERT(0 == ret3);

        // Tell recipient to notify me when I can send again
        SkalMsg* msg4 = SkalMsgCreate("skal-ntf-xon",
                SkalMsgRecipient(msg), 0, NULL);
        SkalMsgSetInternalFlags(msg4, SKAL_MSG_IFLAG_INTERNAL);
        int ret4 = SkalQueuePush(recipient->queue, msg4);
        SKALASSERT(0 == ret4);
    }
    // else: Message successfully sent => Nothing else to do

    SkalPlfMutexUnlock(gMutex);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


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
        bool internalOnly = !CdsMapIsEmpty(priv->xoff);
        SkalMsg* msg = SkalQueuePop_BLOCKING(thread->queue, internalOnly);

        if (SkalMsgInternalFlags(msg) & SKAL_MSG_IFLAG_INTERNAL) {
            if (!skalThreadHandleInternalMsg(priv, msg)) {
                stop = true;
            }
        }

        // TODO: handle the case where thread A sends a message to thread B to
        // be notified of when it can xon again, but thread B terminates before
        // the ntf-xon message reaches it.
        if (    !CdsMapIsEmpty(priv->ntfXon)
             && !SkalQueueIsHalfFull(thread->queue) ) {
            // Some threads are waiting for my queue not to be full anymore
            skalThreadSendXon(priv);
        }

        int64_t start_ns = SkalNow_ns();
        if (!thread->cfg.processMsg(thread->cfg.cookie, msg)) {
            stop = true;
        }
        int64_t duration_ns = SkalNow_ns() - start_ns;
        // TODO: Do something with `duration_ns`
        (void)duration_ns;

        SkalMsgUnref(msg);
    } // Thread loop

    if (strncmp(thread->cfg.name, "skal-master", SKAL_NAME_MAX) != 0) {
        // I am not the master thread: tell the master thread I'm finished
        // TODO: add a list of other threads waiting on me, so that the master
        // thread can free them
        SkalMsg* msg = SkalMsgCreate("skal-terminated", "skal-master", 0, NULL);
        SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        int ret = SkalQueuePush(gMaster->queue, msg);
        SKALASSERT(0 == ret);

    } else {
        // I am the master thread: unblock the global queue now
        SkalMsg* msg = SkalMsgCreate("skal-terminated", "skal-main", 0, NULL);
        SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        int ret = SkalQueuePush(gGlobalQueue, msg);
        SKALASSERT(0 == ret);
    }
}


static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg)
{
    SKALASSERT(priv != NULL);

    bool stop = false;

    const char* type = SkalMsgType(msg);
    if (strncmp(type, "skal-xoff", SKAL_NAME_MAX) == 0) {
        // A thread is telling me to stop sending to it
        const char* origin = SkalMsgGetString(msg, "origin");
        skalXoffItem* xoffItem = (skalXoffItem*)CdsMapSearch(priv->xoff,
                (void*)origin);
        if (xoffItem != NULL) {
            xoffItem->count++;
        } else {
            xoffItem = SkalMallocZ(sizeof(*xoffItem));
            strncpy(xoffItem->origin, origin, sizeof(xoffItem->origin) - 1);
            xoffItem->count = 1;
            bool bret = CdsMapInsert(priv->xoff,
                    (void*)xoffItem->origin, &xoffItem->item);
            SKALASSERT(bret);
        }

    } else if (strncmp(type, "skal-xon", SKAL_NAME_MAX) == 0) {
        // A thread is telling me I can resume sending to it
        const char* sender = SkalMsgSender(msg);
        skalXoffItem* xoffItem = (skalXoffItem*)CdsMapSearch(priv->xoff,
                (void*)sender);
        if (NULL == xoffItem) {
            // Received an unexpected xon message...
            // TODO: Decide what to do with that
        } else {
            xoffItem->count--;
            if (xoffItem->count <= 0) {
                CdsMapItemRemove(priv->xoff, &xoffItem->item);
            }
        }

    } else if (strncmp(type, "skal-ntf-xon", SKAL_NAME_MAX) == 0) {
        // A thread is telling me I should notify it when it can send messages
        // again to me
        const char* origin = SkalMsgGetString(msg, "origin");
        skalNtfXonItem* ntfXonItem = (skalNtfXonItem*)CdsMapSearch(
                priv->ntfXon, (void*)origin);
        if (ntfXonItem != NULL) {
            ntfXonItem->count++;
        } else {
            ntfXonItem = SkalMallocZ(sizeof(*ntfXonItem));
            strncpy(ntfXonItem->origin, origin, sizeof(ntfXonItem->origin) - 1);
            ntfXonItem->count = 1;
            bool bret = CdsMapInsert(priv->ntfXon,
                    (void*)ntfXonItem->origin, &ntfXonItem->item);
            SKALASSERT(bret);
        }

    } else if (strncmp(type, "skal-terminate", SKAL_NAME_MAX) == 0) {
        stop = true;
    }

    return stop;
}


static void skalThreadSendXon(skalThreadPrivate* priv)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(priv->thread != NULL);

    CdsMapIterator* iter = CdsMapIteratorCreate(priv->ntfXon, true);
    for (skalNtfXonItem* ntfXonItem = (skalNtfXonItem*)CdsMapIteratorNext(iter, NULL);
            ntfXonItem != NULL;
            ntfXonItem = (skalNtfXonItem*)CdsMapIteratorNext(iter, NULL) ) {
        // NB: Send as many "xon" messages as "xoff" messages I received
        for (int i = 0; i < ntfXonItem->count; i++) {
            SkalMsg* msg = SkalMsgCreate("skal-xon",
                    ntfXonItem->origin, 0, NULL);
            SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(msg, "origin", priv->thread->cfg.name);
            SkalMsgSend(msg);
        }
    }
    CdsMapIteratorDestroy(iter);

    CdsMapClear(priv->ntfXon);
}


static bool skalMasterProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strncmp(SkalMsgRecipient(msg), "skal-master", SKAL_NAME_MAX) != 0) {
        // This message is not for the master thread
        //  => Forward it
        // TODO: from here: routing
        SKALPANIC_MSG("Routing not yet implemented!");
        return false;
    }

    bool stop = false;
    const char* type = SkalMsgType(msg);
    if (strncmp(type, "skal-terminate", SKAL_NAME_MAX) == 0) {
        // I have been asked to terminate myself
        //  => Tell all threads to terminate themselves
        SkalPlfMutexLock(gMutex);
        if (CdsMapIsEmpty(gThreads)) {
            stop = true;
        } else {
            CdsMapIterator* iter = CdsMapIteratorCreate(gThreads, true);
            for (   CdsMapItem* item = CdsMapIteratorNext(iter, NULL);
                    item != NULL;
                    item = CdsMapIteratorNext(iter, NULL) ) {
                SkalThread* thread = (SkalThread*)item;
                if (!SkalQueueIsInShutdownMode(thread->queue)) {
                    SkalQueueShutdown(thread->queue);
                    SkalMsg* msg = SkalMsgCreate("skal-terminate",
                            thread->cfg.name, 0, NULL);
                    SkalMsgSetInternalFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
                    int ret = SkalQueuePush(thread->queue, msg);
                    SKALASSERT(0 == ret);
                }
            } // for each thread in this process
        }
        SkalPlfMutexUnlock(gMutex);

    } else if (strncmp(type, "skal-terminated", SKAL_NAME_MAX) == 0) {
        // A thread is telling me it just finished
        SkalPlfMutexLock(gMutex);
        CdsMapRemove(gThreads, (void*)SkalMsgSender(msg));
        if (CdsMapIsEmpty(gThreads)) {
            stop = true;
        }
        SkalPlfMutexUnlock(gMutex);
    }

    return stop;
}
