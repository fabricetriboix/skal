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
#include "skal-net.h"
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
    char peer[SKAL_NAME_MAX];

    /** Last time we sent a 'skal-ntf-xon' message to `peer` */
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
    char peer[SKAL_NAME_MAX];
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


/** Send a message
 *
 * If the recipient is within this process, the message is always successfully
 * sent, you lose ownership of `msg`, and this function returns `true`.
 *
 * If the recipient is outside this process, the behaviour of this function
 * depends on the value of `fallBackToSkald`. If `true`, the message is sent to
 * the local skald, which is always a successful operation, you lose ownership
 * of `msg` and this function returns `true`.
 *
 * If the recipient is outside this process and `fallBackToSkald` is `false`,
 * the message is not sent, you retain ownership of `msg`, and this function
 * returns `false`.
 *
 * @param msg             [in,out] Message to send; must not be NULL
 * @param fallBackToSkald [in]     Send `msg` to skald if recipient is outside
 *                                 this process
 *
 * @return `true` if message sent, `false` if not
 */
static bool skalDoSendMsg(SkalMsg* msg, bool fallBackToSkald);


/** Create a thread
 *
 * @param cfg [in] Description of thread to create; must not be NULL and must
 *                 comply with the restrictions indicated in `skal.h`
 *
 * @return The newly created thread; this function never returns NULL
 */
static SkalThread* skalDoCreateThread(const SkalThreadCfg* cfg);


/** Function to unreference a thread structure
 *
 * Please note this function will not terminate the thread. This function blocks
 * until the thread has terminated.
 */
static void skalThreadUnref(SkalThread* thread);


/** Function to unreference a map item where the item just needs to be freed */
static void skalMapUnrefFree(CdsMapItem* item);


/** Run a SKAL thread
 *
 * This function should not be used for the `skal-master` thread, use
 * `skalMasterThreadRun()` instead.
 *
 * @param arg [in] Argument; actually a `SkalThread*` structure representing
 *                 this very thread
 */
static void skalThreadRun(void* arg);


/** Utility function for a thread: handle internal message
 *
 * This function assumes the current thread is not `skal-master`; this function
 * should not be called for `skal-master`.
 *
 * @param priv [in,out] Thread private data
 * @param msg  [in]     Received message
 *
 * @return `false` to stop this thread now, otherwise `true`
 */
static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg);


/** Utility function for a thread: send "xon" messages to all blocked threads
 *
 * This function assumes the current thread is not `skal-master`; this function
 * should not be called for `skal-master`.
 *
 * @param priv [in,out] Thread private data
 */
static void skalThreadSendXon(skalThreadPrivate* priv);


/** Utility function for a thread: retry "ntf-xon" messages
 *
 * This function assumes the current thread is not `skal-master`; this function
 * should not be called for `skal-master`.
 *
 * For all threads we are blocked on and have timed out, we re-send a "ntf-xon"
 * to the blocking thread to tell it to unblock us.
 *
 * @param priv   [in,out] Thread private data
 * @param now_us [in]     Current time, in us
 */
static void skalThreadRetryNtfXon(skalThreadPrivate* priv, int64_t now_us);


/** Run the `skal-master` thread
 *
 * Please note the `skal-master` thread does not participate in the xon/xoff
 * mechanism. More exactly, a thread should never send a `skal-ntf-xon` message
 * to `skal-master`.
 *
 * @param arg [in] Argument; actually a `SkalThread*` structure representing
 *                 this very thread
 */
static void skalMasterThreadRun(void* arg);


/** `skal-master` only: route a message sent to this process by the local skald
 *
 * This function takes ownership of `msg`.
 *
 * @param msg [in,out] Message to route; must not be NULL
 */
static void skalMasterRouteMsg(SkalMsg* msg);


/** `skal-master` only: process a message received from within this process
 *
 * This function takes ownership of `msg`.
 *
 * @param msg [in] Message to process
 *
 * @return `false` if the `skal-master` thread must terminate immediately,
 *         `true` if it should continue
 */
static bool skalMasterProcessMsg(SkalMsg* msg);


/** `skal-master` hook for queue push
 *
 * This is used to send a byte on the pipe to wake `skal-master` up; this is
 * because `skal-master` does not use the signaling facility of `skal-queue`.
 */
static void skalMasterPushHook(void* cookie);



/*------------------+
 | Global variables |
 +------------------*/


/** Name of this process */
static char gProcessName[SKAL_NAME_MAX] = "";


/** Mutex to protect the `gThreads` map */
static SkalPlfMutex* gMutex = NULL;


/** Map of threads
 *
 * Items are of type `SkalThread`. Keys are of type `const char*` and are the
 * name of the threads, i.e. `SkalThread->cfg.name`.
 *
 * The `skal-master` thread is not in this map.
 */
static CdsMap* gThreads = NULL;


/** Are we in the process of terminating this process and all its threads? */
static bool gTerminating = false;


/** Master thread
 *
 * This is not in the `gThreads` map because of its special role.
 */
static SkalThread* gMaster = NULL;


/** State of the master thread: running or not
 *
 * This is used to unblock the `main()` thread.
 */
static bool gMasterRunning = false;


/** Whether to cancel a `SkalThreadPause()` */
static bool gCancelPause = false;


/** Mutex to protect the `gMasterRunning` variable */
static SkalPlfMutex* gMasterRunningMutex = NULL;


/** CondVar to wake up a `SkalThreadPause()` when skal-master exits */
static SkalPlfCondVar* gMasterRunningCondVar = NULL;


/** Global queue
 *
 * This global queue is used to communicate between the original thread (i.e.
 * the one from the `main()` function) and the master thread.
 *
 * In practice, it's used only in the end to indicate that all threads have
 * terminated.
 */
static SkalQueue* gGlobalQueue = NULL;


/** Sockets
 *
 * In actually, there are only two sockets in this set:
 *  - A UNIX socket to communicate with the local skald
 *  - A pipe to wake `skal-master` up when it receives messages from other
 *    threads
 */
static SkalNet* gNet = NULL;


/** Id of UNIX socket to communicate with skald */
static int gSockid = -1;


/** Id of read-side of skal-master pipe */
static int gPipeServerId = -1;


/** Id of write-side of skal-master pipe */
static int gPipeClientId = -1;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


bool SkalThreadInit(const char* skaldPath)
{
    SKALASSERT(NULL == gMutex);
    SKALASSERT(NULL == gThreads);
    SKALASSERT(NULL == gMaster);
    SKALASSERT(NULL == gGlobalQueue);
    SKALASSERT(NULL == gNet);

    gTerminating = false;

    SkalPlfGetPThreadName(gProcessName, sizeof(gProcessName));

    // Create UNIX socket and connect to skald
    if (NULL == skaldPath) {
        skaldPath = SKAL_DEFAULT_SKALD_PATH;
    }
    gNet = SkalNetCreate(NULL);
    SkalNetAddr addr;
    SKALASSERT(strlen(skaldPath) < sizeof(addr.unix.path));
    addr.type = SKAL_NET_TYPE_UNIX_SEQPACKET;
    strcpy(addr.unix.path, skaldPath);
    gSockid = SkalNetCommCreate(gNet, NULL, &addr, 0, NULL, 0);
    if (gSockid < 0) {
        SkalNetDestroy(gNet);
        gNet = NULL;
        return false;
    }
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    if (SKAL_NET_EV_NOT_ESTABLISHED == event->type) {
        SkalNetEventUnref(event);
        SkalNetDestroy(gNet);
        gNet = NULL;
        return false;
    }
    SKALASSERT(SKAL_NET_EV_ESTABLISHED == event->type);
    SKALASSERT(gSockid == event->sockid);
    SkalNetEventUnref(event);

    gMutex = SkalPlfMutexCreate();

    char name[SKAL_NAME_MAX];
    snprintf(name, sizeof(name), "%s-queue", gProcessName);
    gGlobalQueue = SkalQueueCreate(name, SKAL_THREADS_MAX);

    snprintf(name, sizeof(name), "%s-threads", gProcessName);
    gThreads = CdsMapCreate(name,              // name
                            SKAL_THREADS_MAX,  // capacity
                            SkalStringCompare, // compare
                            NULL,              // cookie
                            NULL,              // keyUnref
                            (void(*)(CdsMapItem*))skalThreadUnref); // itemUnref

    // Create pipe
    addr.type = SKAL_NET_TYPE_PIPE;
    gPipeServerId = SkalNetServerCreate(gNet, &addr, 0, NULL, 0);
    SKALASSERT(gPipeServerId >= 0);
    event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    gPipeClientId = event->conn.commSockid;
    SkalNetEventUnref(event);

    // Create master thread
    gMasterRunningMutex = SkalPlfMutexCreate();
    gMasterRunningCondVar = SkalPlfCondVarCreate();
    gMaster = SkalMallocZ(sizeof(*gMaster));
    snprintf(gMaster->cfg.name, sizeof(gMaster->cfg.name), "skal-master");
    gMaster->cfg.queueThreshold = SKAL_MSG_LIST_MAX;
    gMaster->queue = SkalQueueCreate("skal-master-queue",
            gMaster->cfg.queueThreshold);
    SkalQueueSetPushHook(gMaster->queue, skalMasterPushHook, NULL);
    gMaster->thread = SkalPlfThreadCreate(gMaster->cfg.name,
            skalMasterThreadRun, NULL);

    // Wait for master thread to finish initialisation
    SkalMsg* msg = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strcmp(SkalMsgType(msg), "skal-master-init-done") == 0);
    SkalMsgUnref(msg);
    return true;
}


void SkalThreadExit(void)
{
    // Tell the `skal-master` thread to terminate. It will trigger and handle
    // the termination of all threads in this process. Then wait for
    // `skal-master` to terminate.

    gTerminating = true;
    SKALASSERT(gMaster != NULL);
    SkalMsg* msg = SkalMsgCreate("skal-master-terminate",
            "skal-master", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gMaster->queue, msg);

    SkalMsg* resp = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strcmp(SkalMsgType(resp), "skal-master-terminated") == 0);
    SkalMsgUnref(resp);

    skalThreadUnref(gMaster);
    SKALASSERT(CdsMapIsEmpty(gThreads)); // All threads must have terminated now
    gMaster = NULL;

    // Release other global variables

    CdsMapDestroy(gThreads);
    gThreads = NULL;

    SkalQueueDestroy(gGlobalQueue);
    gGlobalQueue = NULL;

    SkalNetDestroy(gNet);
    gNet = NULL;

    SkalPlfMutexDestroy(gMutex);
    gMutex = NULL;

    if (gMasterRunningMutex != NULL) {
        SkalPlfMutexDestroy(gMasterRunningMutex);
        gMasterRunningMutex = NULL;
    }

    if (gMasterRunningCondVar != NULL) {
        SkalPlfCondVarDestroy(gMasterRunningCondVar);
        gMasterRunningCondVar = NULL;
    }
}


bool SkalThreadPause(void)
{
    SkalPlfMutexLock(gMasterRunningMutex);
    gCancelPause = false;
    while (gMasterRunning && !gCancelPause) {
        SkalPlfCondVarWait(gMasterRunningCondVar, gMasterRunningMutex);
    }
    SkalPlfMutexUnlock(gMasterRunningMutex);
    return !gMasterRunning;
}


void SkalThreadCancel(void)
{
    SkalPlfMutexLock(gMasterRunningMutex);
    gCancelPause = true;
    SkalPlfMutexUnlock(gMasterRunningMutex);
    SkalPlfCondVarSignal(gMasterRunningCondVar);
}


void SkalThreadCreate(const SkalThreadCfg* cfg)
{
    if (!gTerminating) {
        // `SkalThreadInit()` must have been called first
        SKALASSERT(gThreads != NULL);

        // NB: The domain name will be appended to the thread name
        SkalThread* thread = skalDoCreateThread(cfg);
        SkalPlfMutexLock(gMutex);
        CdsMapItem* item = CdsMapSearch(gThreads, (void*)(thread->cfg.name));
        SKALASSERT(NULL == item);
        bool inserted = CdsMapInsert(gThreads,
                (void*)(thread->cfg.name), &thread->item);
        SKALASSERT(inserted);
        SkalPlfMutexUnlock(gMutex);
    }
}


void SkalMsgSend(SkalMsg* msg)
{
    bool sent = skalDoSendMsg(msg, true);
    SKALASSERT(sent);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static bool skalDoSendMsg(SkalMsg* msg, bool fallBackToSkald)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(gMaster != NULL);

    SkalPlfMutexLock(gMutex);
    const char* dst = SkalMsgRecipient(msg);
    SkalThread* recipient;
    // NB: The domain name will have been added to the recipient
    if (strncmp(dst, "skal-master", 11) == 0) {
        recipient = gMaster;
    } else if (strncmp(dst, "skald", 5) == 0) {
        recipient = NULL;
    } else {
        recipient = (SkalThread*)CdsMapSearch(gThreads, (void*)dst);
    }
    SkalPlfMutexUnlock(gMutex);

    bool sent = false;
    if (recipient != NULL) {
        SkalQueuePush(recipient->queue, msg);
        sent = true;
        if (    SkalQueueIsOverHighThreshold(recipient->queue)
             && !(SkalMsgIFlags(msg) & SKAL_MSG_IFLAG_INTERNAL)
             && (strncmp(recipient->cfg.name, "skal-master", 11) != 0)) {
            // Recipient queue is full
            //  => Enter XOFF mode by sending an xoff msg to myself
            //     EXCEPT if this is an internal message or the recipient is
            //     `skal-master`
            skalThreadPrivate* priv = SkalPlfThreadGetSpecific();
            if (priv != NULL) {
                // NB: The current thread might not be managed by SKAL; in that
                // case, it will not be throttled by SKAL.
                SKALASSERT(priv->thread != NULL);
                SkalMsg* msg2 = SkalMsgCreate("skal-xoff",
                        priv->thread->cfg.name, 0, NULL);
                SkalMsgSetIFlags(msg2, SKAL_MSG_IFLAG_INTERNAL);
                SkalMsgSetSender(msg2, SkalMsgRecipient(msg));
                SkalQueuePush(priv->thread->queue, msg2);
            }
        }
    } else if (fallBackToSkald) {
        // Recipient is not in this process => Send to skald for routing
        // NB: This may block the current thread for a very short while if skald
        // is overloaded.
        char* json = SkalMsgToJson(msg);
        SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, gSockid,
                json, strlen(json) + 1);
        free(json);
        SKALASSERT(SKAL_NET_SEND_OK == result);
        sent = true;
        SkalMsgUnref(msg);
    }

    return sent;
}


static SkalThread* skalDoCreateThread(const SkalThreadCfg* cfg)
{
    SKALASSERT(cfg != NULL);
    SKALASSERT(SkalIsAsciiString(cfg->name, sizeof(cfg->name)));
    SKALASSERT(strlen(cfg->name) > 0);
    SKALASSERT(strncmp(cfg->name, "skal-master", 11) != 0);
    SKALASSERT(cfg->processMsg != NULL);

    SkalThread* thread = SkalMallocZ(sizeof(*thread));
    thread->cfg = *cfg;
    int n = strlen(thread->cfg.name);
    int remaining = sizeof(thread->cfg.name) - n;
    n = snprintf(&(thread->cfg.name[n]), remaining, "@%s", SkalDomain());
    SKALASSERT(n < remaining);
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
    SKALASSERT(strncmp(thread->cfg.name, "skal-master", 11) != 0);
    SkalPlfThreadSetName(thread->cfg.name);

    skalThreadPrivate* priv = SkalMallocZ(sizeof(*priv));
    priv->thread = thread;

    priv->xoff = CdsMapCreate(NULL,              // name
                              SKAL_XOFF_MAX,     // capacity
                              SkalStringCompare, // compare
                              NULL,              // cookie
                              NULL,              // keyUnref
                              skalMapUnrefFree); // itemUnref

    priv->ntfXon = CdsMapCreate(NULL,              // name
                                SKAL_XOFF_MAX,     // capacity
                                SkalStringCompare, // compare
                                NULL,              // cookie
                                NULL,              // keyUnref
                                skalMapUnrefFree); // itemUnref

    SkalPlfThreadSetSpecific(priv);

    // Inform skald that this thread is born
    SkalMsg* msg = SkalMsgCreate("skal-born", "skald", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalMsgSend(msg);

    bool stop = false;
    while (!stop) {
        // If I have some pending `xoff`, I should only process internal
        // messages and leave all other messages pending in the queue.
        bool internalOnly = !CdsMapIsEmpty(priv->xoff);
        // TODO: Add timer to periodically retry sending `skal-ntf-xon`
        // messages instead of hoping for spurious interruptions
        msg = SkalQueuePop_BLOCKING(thread->queue, internalOnly);
        SKALASSERT(msg != NULL);

        if (SkalMsgIFlags(msg) & SKAL_MSG_IFLAG_INTERNAL) {
            if (!skalThreadHandleInternalMsg(priv, msg)) {
                stop = true;
            }
        }

        if (    !CdsMapIsEmpty(priv->ntfXon)
             && !SkalQueueIsOverLowThreshold(thread->queue)
             && !stop) {
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
    // Free up any threads blocked on me
    skalThreadSendXon(priv);

    // Notify skald
    msg = SkalMsgCreate("skal-died", "skald", 0, NULL);
    SkalMsgSend(msg);

    // Notify skal-master
    msg = SkalMsgCreate("skal-terminated", "skal-master", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gMaster->queue, msg);

    free(priv);
}


static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(msg != NULL);

    bool ok = true;

    const char* type = SkalMsgType(msg);
    if (strcmp(type, "skal-xoff") == 0) {
        // A thread is telling me to stop sending to it
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->xoff, (void*)sender);
        if (NULL == item) {
            skalXoffItem* xoffItem = SkalMallocZ(sizeof(*xoffItem));
            strncpy(xoffItem->peer, sender, sizeof(xoffItem->peer) - 1);
            bool inserted = CdsMapInsert(priv->xoff,
                    (void*)xoffItem->peer, &xoffItem->item);
            SKALASSERT(inserted);
        }

        // Tell originating thread to notify me when I can send again
        SkalMsg* msg2 = SkalMsgCreate("skal-ntf-xon", sender, 0, NULL);
        SkalMsgSetIFlags(msg2, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgSend(msg2);

    } else if (strcmp(type, "skal-xon") == 0) {
        // A thread is telling me I can resume sending to it
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->xoff, (void*)sender);
        if (NULL == item) {
            // Received an unexpected `xon` message; this can happen in case of
            // retries, just ignore it
        } else {
            CdsMapItemRemove(priv->xoff, item);
        }

    } else if (strcmp(type, "skal-ntf-xon") == 0) {
        // A thread is telling me I should notify it when it can send messages
        // again to me
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->ntfXon, (void*)sender);
        if (NULL == item) {
            skalNtfXonItem* ntfXonItem = SkalMallocZ(sizeof(*ntfXonItem));
            strncpy(ntfXonItem->peer, sender, sizeof(ntfXonItem->peer) - 1);
            bool inserted = CdsMapInsert(priv->ntfXon,
                    (void*)ntfXonItem->peer, &ntfXonItem->item);
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
    for (CdsMapItem* item = CdsMapIteratorNext(priv->ntfXon, NULL);
            item != NULL;
            item = CdsMapIteratorNext(priv->ntfXon, NULL)) {
        skalNtfXonItem* ntfXonItem = (skalNtfXonItem*)item;
        SkalMsg* msg = SkalMsgCreate("skal-xon", ntfXonItem->peer, 0, NULL);
        SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgSend(msg);
    }

    CdsMapClear(priv->ntfXon);
}


static void skalThreadRetryNtfXon(skalThreadPrivate* priv, int64_t now_us)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(priv->xoff != NULL);

    CdsMapIteratorReset(priv->xoff, true);
    for (CdsMapItem* item = CdsMapIteratorNext(priv->xoff, NULL);
            item != NULL;
            item = CdsMapIteratorNext(priv->xoff, NULL)) {
        skalXoffItem* xoffItem = (skalXoffItem*)item;
        int64_t elapsed_us = now_us - xoffItem->lastNtfXonTime_us;
        if (elapsed_us > priv->thread->cfg.xoffTimeout_us) {
            // We waited quite long for a `skal-xon` message...
            //  => Poke the blocking thread
            SkalMsg* msg = SkalMsgCreate("skal-ntf-xon",
                    xoffItem->peer, 0, NULL);
            SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgSend(msg);
            xoffItem->lastNtfXonTime_us = now_us;
        }
    }
}


static void skalMasterThreadRun(void* arg)
{
    SkalPlfThreadSetName("skal-master");

    skalThreadPrivate* priv = SkalMallocZ(sizeof(*priv));
    priv->thread = gMaster;
    SkalPlfThreadSetSpecific(priv);

    // Tell skald the master thread for this process is starting
    SkalMsg* msg = SkalMsgCreate("skal-init-master-born", "skald", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalMsgAddString(msg, "name", gProcessName);
    SkalMsgSend(msg);

    // Wait for skald to tell us what is its domain name
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_IN == event->type);
    SKALASSERT(event->in.data != NULL);
    SKALASSERT(event->in.size_B > 0);
    char* json = (char*)(event->in.data);
    json[event->in.size_B - 1] = '\0';
    msg = SkalMsgCreateFromJson(json);
    if (NULL == msg) {
        SKALPANIC_MSG("Invalid message received from skald; wrong message format version?");
    }
    SKALASSERT(strcmp(SkalMsgType(msg), "skal-init-domain") == 0);
    SkalSetDomain(SkalMsgGetString(msg, "domain"));
    SkalMsgUnref(msg);
    SkalNetEventUnref(event);

    // Unblock `SkalThreadInit()`
    SkalPlfMutexLock(gMasterRunningMutex);
    gMasterRunning = true;
    SkalPlfMutexUnlock(gMasterRunningMutex);
    msg = SkalMsgCreate("skal-master-init-done", "skal-main", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gGlobalQueue, msg);

    bool stop = false;
    while (!stop) {
        // `skal-master` may receive messages from either its queue or the
        // UNIX socket used to communicate with skald.
        //  => We use `skal-net` as the single blocking point instead of
        //     `skal-queue`
        SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
        SKALASSERT(event != NULL);

        switch (event->type) {
        case SKAL_NET_EV_DISCONN :
            SKALPANIC_MSG("skald disconnected!");
            break;

        case SKAL_NET_EV_IN :
            if (gSockid == event->sockid) {
                // This is a message that has been routed to this process by the
                // local skald
                SKALASSERT(event->in.data != NULL);
                SKALASSERT(event->in.size_B > 0);
                json = (char*)(event->in.data);
                json[event->in.size_B - 1] = '\0';
                msg = SkalMsgCreateFromJson(json);
                if (NULL == msg) {
                    SKALPANIC_MSG("Invalid message received from skald; wrong message format version?");
                }
                skalMasterRouteMsg(msg);

            } else {
                // This is a message sent to us from within this process
                // NB: It is possible that more than one message has been queued
                // up, in which case the pipe we use to wake skal-master up will
                // have more than one character in it; actually, it will have
                // one character per message.
                SKALASSERT(gPipeServerId == event->sockid);
                for (int i = 0; (i < event->in.size_B) && !stop; i++) {
                    msg = SkalQueuePop(gMaster->queue, false);
                    SKALASSERT(msg != NULL);
                    if (!skalMasterProcessMsg(msg)) {
                        stop = true;
                    }
                }
            }
            break;

        case SKAL_NET_EV_ERROR :
            if (gSockid == event->sockid) {
                SKALPANIC_MSG("Error reported on skald socket");
            } else {
                SKALPANIC_MSG("Error reported on pipe");
            }
            break;

        default :
            SKALPANIC_MSG("Unexpected SkalNet event %d", (int)event->type);
        }
        SkalNetEventUnref(event);
    } // Thread loop

    // This thread is now terminated
    SkalPlfMutexLock(gMasterRunningMutex);
    gMasterRunning = false;
    SkalPlfMutexUnlock(gMasterRunningMutex);
    SkalPlfCondVarSignal(gMasterRunningCondVar);
    msg = SkalMsgCreate("skal-master-terminated", "skal-main", 0, NULL);
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gGlobalQueue, msg);
    free(priv);
}


static void skalMasterRouteMsg(SkalMsg* msg)
{
    bool sent = skalDoSendMsg(msg, false);
    if (!sent) {
        // Recipient is not in this process. This would normally be caught by
        // the local skald, but let's handle it nevertheless.
        if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP) {
            // The sender wants to be notified of dropped message
            SkalMsg* resp = SkalMsgCreate("skal-error-drop",
                    SkalMsgSender(msg), 0, NULL);
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(resp, "reason", "no recipient");
            SkalMsgAddFormattedString(resp, "extra",
                    "Thread %s does not exist", SkalMsgRecipient(msg));
            sent = skalDoSendMsg(resp, true);
            SKALASSERT(sent);
        }
    }
}


static bool skalMasterProcessMsg(SkalMsg* msg)
{
    SKALASSERT(strncmp(SkalMsgRecipient(msg), "skal-master", 11) == 0);

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
                SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
                SkalQueuePush(thread->queue, msg);
            } // for each thread in this process
        }
        SkalPlfMutexUnlock(gMutex);
        SkalMsgUnref(msg);

    } else if (strcmp(type, "skal-terminated") == 0) {
        // A thread is telling me it just finished
        SkalPlfMutexLock(gMutex);
        CdsMapRemove(gThreads, (void*)SkalMsgSender(msg));
        if (CdsMapIsEmpty(gThreads)) {
            ok = false;
        }
        SkalPlfMutexUnlock(gMutex);
        SkalMsgUnref(msg);
    }

    return ok;
}


static void skalMasterPushHook(void* cookie)
{
    SKALASSERT(gNet != NULL);
    (void)cookie;
    char c = 'x';
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, gPipeClientId, &c, 1);
    SKALASSERT(SKAL_NET_SEND_OK == result);
}
