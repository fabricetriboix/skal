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
typedef struct {
    CdsMapItem item;

    /** Name of the recipient that got its queue full because of me */
    char* peer;

    /** Last time we sent a 'skal-ntf-xon' message to `peer` */
    int64_t lastNtfXonTime_us;
} skalXoffItem;


/** Structure that represents an item of the `SkalThread.ntfXon` map
 *
 * NB: We do not count references because we know by design it's referenced only
 * once at creation.
 */
typedef struct {
    CdsMapItem item;

    /** Name of the thread to notify */
    char* peer;
} skalNtfXonItem;


/** Structure that defines a thread
 *
 * Please note that except when first created and after termination, this
 * structure is access only by the thread itself.
 */
struct SkalThread {
    CdsMapItem item;
    int ref;

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
typedef struct {
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


/** Atomically find the recipient thread of a message
 *
 * @param msg [in] The message to query; must not be NULL
 *
 * @return The recipient thread, or NULL if the recipient is not in this
 *         process; if not NULL, the thread structure would have its reference
 *         counter incremented, so you need to call `skalThreadUnref()` on it
 *         when finished.
 */
static SkalThread* skalRecipientThread(const SkalMsg* msg);


/** Send a message to skald for routing
 *
 * This function takes ownership of `msg`.
 *
 * NB: This may block the current thread for a very short time if skald is
 * overloaded.
 *
 * @param msg [in] Message to send to skald; must not be NULL
 */
static void skalSendMsgToSkald(SkalMsg* msg);


/** Send a message to a thread in this process
 *
 * This function takes ownership of `msg`.
 *
 * This function will check whether the recipient queue is full. If yes, it will
 * send an xoff message to the sender.
 *
 * @param msg    [in] The message to send; must not be NULL
 * @param thread [in] To whom to send this message to
 */
static void skalSendMsgToThread(SkalMsg* msg, SkalThread* thread);


/** Create a thread
 *
 * @param cfg [in] Description of thread to create; must not be NULL and must
 *                 comply with the restrictions indicated in `skal.h`
 *
 * @return The newly created thread; this function never returns NULL
 */
static SkalThread* skalDoCreateThread(const SkalThreadCfg* cfg);


/** Function to reference a thread structure */
static void skalThreadRef(SkalThread* thread);


/** Function to unreference a thread structure
 *
 * Please note this function will not terminate the thread. This function blocks
 * until the thread has terminated.
 */
static void skalThreadUnref(SkalThread* thread);


/** Function to unreference a `skalXoffItem` */
static void skalMapXoffUnref(CdsMapItem* mitem);


/** Function to unreference a `skalNtfXonItem` */
static void skalMapNtfXonUnref(CdsMapItem* mitem);


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
static char* gProcessName = NULL;


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


/** Id of read-side of skal-master pipe
 *
 * This is used to wake up skal-master when a message is pushed into its queue.
 * This is necessary because the only point of synchronisation for the
 * skal-master thread is its `SkalNet` set of sockets.
 */
static int gPipeServerId = -1;


/** Id of write-side of skal-master pipe */
static int gPipeClientId = -1;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


bool SkalThreadInit(const char* skaldUrl)
{
    SKALASSERT(NULL == gProcessName);
    SKALASSERT(NULL == gMutex);
    SKALASSERT(NULL == gThreads);
    SKALASSERT(NULL == gMaster);
    SKALASSERT(NULL == gGlobalQueue);
    SKALASSERT(NULL == gNet);

    gTerminating = false;

    gProcessName = SkalPlfGetSystemThreadName();

    // Connect to skald
    if (NULL == skaldUrl) {
        skaldUrl = SKAL_DEFAULT_SKALD_URL;
    }
    gNet = SkalNetCreate(NULL);
    gSockid = SkalNetCommCreate(gNet, NULL, skaldUrl, 0, NULL, 0);
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

    char* name = SkalSPrintf("%s-queue", gProcessName);
    gGlobalQueue = SkalQueueCreate(name, SKAL_DEFAULT_QUEUE_THRESHOLD);
    free(name);

    gThreads = CdsMapCreate(NULL,              // name
                            0,                 // capacity
                            SkalStringCompare, // compare
                            NULL,              // cookie
                            NULL,              // keyUnref
                            (void(*)(CdsMapItem*))skalThreadUnref); // itemUnref

    // Create pipe to wake up skal-master when a message is pushed into its q
    gPipeServerId = SkalNetServerCreate(gNet, "pipe://", 0, NULL, 0);
    SKALASSERT(gPipeServerId >= 0);
    event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    gPipeClientId = event->conn.commSockid;
    SkalNetEventUnref(event);

    // Create skal-master thread
    gMasterRunningMutex = SkalPlfMutexCreate();
    gMasterRunningCondVar = SkalPlfCondVarCreate();
    gMaster = SkalMallocZ(sizeof(*gMaster));
    gMaster->ref = 1;
    gMaster->cfg.name = SkalStrdup("skal-master");
    gMaster->cfg.queueThreshold = SKAL_DEFAULT_QUEUE_THRESHOLD;
    gMaster->queue = SkalQueueCreate("skal-master-queue",
            gMaster->cfg.queueThreshold);
    SkalQueueSetPushHook(gMaster->queue, skalMasterPushHook, NULL);
    gMaster->thread = SkalPlfThreadCreate(gMaster->cfg.name,
            skalMasterThreadRun, NULL);

    // Wait for master thread to finish initialisation
    SkalMsg* msg = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strcmp(SkalMsgName(msg), "skal-master-init-done") == 0);
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
    SkalMsg* msg = SkalMsgCreate("skal-master-terminate", "skal-master");
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gMaster->queue, msg);

    SkalMsg* resp = SkalQueuePop_BLOCKING(gGlobalQueue, false);
    SKALASSERT(strcmp(SkalMsgName(resp), "skal-master-terminated") == 0);
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

    SkalPlfMutexDestroy(gMasterRunningMutex);
    gMasterRunningMutex = NULL;

    SkalPlfCondVarDestroy(gMasterRunningCondVar);
    gMasterRunningCondVar = NULL;

    free(gProcessName);
    gProcessName = NULL;
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
        CdsMapItem* item = CdsMapSearch(gThreads, thread->cfg.name);
        SKALASSERT(NULL == item);
        bool inserted = CdsMapInsert(gThreads, thread->cfg.name, &thread->item);
        SKALASSERT(inserted);
        SkalPlfMutexUnlock(gMutex);
    }
}


void SkalMsgSend(SkalMsg* msg)
{
    SkalThread* thread = skalRecipientThread(msg);
    if (NULL == thread) {
        skalSendMsgToSkald(msg);
    } else {
        skalSendMsgToThread(msg, thread);
        skalThreadUnref(thread);
    }
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static SkalThread* skalRecipientThread(const SkalMsg* msg)
{
    SKALASSERT(msg != NULL);

    SkalPlfMutexLock(gMutex);

    const char* recipient = SkalMsgRecipient(msg);
    SkalThread* thread;
    // NB: The domain name will have been added to the recipient
    if (SkalStartsWith(recipient, "skal-master")) {
        thread = gMaster;
    } else if (SkalStartsWith(recipient, "skald")) {
        thread = NULL;
    } else {
        thread = (SkalThread*)CdsMapSearch(gThreads, (void*)recipient);
    }
    if (thread != NULL) {
        skalThreadRef(thread);
    }

    SkalPlfMutexUnlock(gMutex);
    return thread;
}


static void skalSendMsgToSkald(SkalMsg* msg)
{
    char* json = SkalMsgToJson(msg);
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, gSockid,
            json, strlen(json) + 1);
    free(json);
    SKALASSERT(SKAL_NET_SEND_OK == result);
    SkalMsgUnref(msg);
}


static void skalSendMsgToThread(SkalMsg* msg, SkalThread* thread)
{
    SKALASSERT(msg != NULL);
    SKALASSERT(thread != NULL);
    SKALASSERT(strcmp(SkalMsgRecipient(msg), thread->cfg.name) == 0);

    SkalQueuePush(thread->queue, msg);

    if (    SkalQueueIsOverHighThreshold(thread->queue)
         && !(SkalMsgIFlags(msg) & SKAL_MSG_IFLAG_INTERNAL)
         && !(SkalMsgFlags(msg) & SKAL_MSG_FLAG_MULTICAST)
         && !SkalStartsWith(thread->cfg.name, "skal-master")
         && !SkalStartsWith(SkalMsgSender(msg), "skal-external")) {
        // Recipient queue is full, tell sender to xoff, unless:
        //  - This is an internal message: the xoff mechanism does not apply
        //    to internal messages and the other end will keeping sending
        //    internal messages anyway; internal messages are rare and
        //    normally spaced in time and we should never have such a
        //    problem
        //  - This is a multicast message: we don't want to stop the sender
        //    to multicast messages as this will impact the other receivers;
        //    in addition, it is expected that receivers of multicast
        //    messages can cope with missing messages
        //  - The recipient is `skal-master`, because `skal-master` does not
        //    implement the xoff mechanism; anyway, all messages sent to
        //    `skal-master` are normally internal messages
        //  - The message sender is not managed by SKAL, in which case the
        //    sender thread will not handle the xoff message correctly
        SkalMsg* xoffMsg = SkalMsgCreate("skal-xoff", SkalMsgSender(msg));
        SkalMsgSetIFlags(xoffMsg, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgSetSender(xoffMsg, SkalMsgRecipient(msg));
        thread = skalRecipientThread(xoffMsg);
        if (NULL == thread) {
            skalSendMsgToSkald(xoffMsg);
        } else {
            SkalQueuePush(thread->queue, xoffMsg);
            skalThreadUnref(thread);
        }
    }
}


static SkalThread* skalDoCreateThread(const SkalThreadCfg* cfg)
{
    SKALASSERT(cfg != NULL);
    SKALASSERT(cfg->name != NULL);
    SKALASSERT(strlen(cfg->name) > 0);
    SKALASSERT(!SkalStartsWith(cfg->name, "skal-master"));
    SKALASSERT(cfg->processMsg != NULL);

    SkalThread* thread = SkalMallocZ(sizeof(*thread));
    thread->ref = 1;
    thread->cfg = *cfg;
    thread->cfg.name = SkalSPrintf("%s@%s", cfg->name, SkalDomain());
    if (thread->cfg.queueThreshold <= 0) {
        thread->cfg.queueThreshold = SKAL_DEFAULT_QUEUE_THRESHOLD;
    }
    if (thread->cfg.xoffTimeout_us <= 0) {
        thread->cfg.xoffTimeout_us = SKAL_DEFAULT_XOFF_TIMEOUT_us;
   }

    char* name = SkalSPrintf("%s-queue", thread->cfg.name);
    thread->queue = SkalQueueCreate(name, thread->cfg.queueThreshold);
    free(name);

    // NB: Create the actual thread last, as it may access the structure
    thread->thread = SkalPlfThreadCreate(thread->cfg.name,
            skalThreadRun, thread);

    return thread;
}


static void skalThreadRef(SkalThread* thread)
{
    SKALASSERT(thread != NULL);
    (thread->ref)++;
}


static void skalThreadUnref(SkalThread* thread)
{
    SKALASSERT(thread != NULL);
    (thread->ref)--;
    if (thread->ref <= 0) {
        SkalPlfThreadJoin(thread->thread);
        SkalQueueDestroy(thread->queue);
        free(thread->cfg.name);
        free(thread);
    }
}


static void skalMapXoffUnref(CdsMapItem* mitem)
{
    skalXoffItem* item = (skalXoffItem*)mitem;
    SKALASSERT(item != NULL);
    free(item->peer);
    free(item);
}


static void skalMapNtfXonUnref(CdsMapItem* mitem)
{
    skalNtfXonItem* item = (skalNtfXonItem*)mitem;
    SKALASSERT(item != NULL);
    free(item->peer);
    free(item);
}


static void skalThreadRun(void* arg)
{
    SKALASSERT(arg != NULL);
    SkalThread* thread = (SkalThread*)arg;
    SKALASSERT(thread->cfg.name != NULL);
    SKALASSERT(!SkalStartsWith(thread->cfg.name, "skal-master"));

    skalThreadPrivate* priv = SkalMallocZ(sizeof(*priv));
    priv->thread = thread;

    priv->xoff = CdsMapCreate(NULL,              // name
                              0,                 // capacity
                              SkalStringCompare, // compare
                              NULL,              // cookie
                              NULL,              // keyUnref
                              skalMapXoffUnref); // itemUnref

    priv->ntfXon = CdsMapCreate(NULL,                // name
                                0,                   // capacity
                                SkalStringCompare,   // compare
                                NULL,                // cookie
                                NULL,                // keyUnref
                                skalMapNtfXonUnref); // itemUnref

    SkalPlfThreadSetSpecific(priv);

    // Inform skald that this thread is born
    SkalMsg* msg = SkalMsgCreate("skal-born", "skald");
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
    msg = SkalMsgCreate("skal-died", "skald");
    SkalMsgSend(msg);

    // Notify skal-master
    msg = SkalMsgCreate("skal-terminated", "skal-master");
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gMaster->queue, msg);

    free(priv);
}


static bool skalThreadHandleInternalMsg(skalThreadPrivate* priv, SkalMsg* msg)
{
    SKALASSERT(priv != NULL);
    SKALASSERT(msg != NULL);

    bool ok = true;

    const char* msgName = SkalMsgName(msg);
    if (strcmp(msgName, "skal-xoff") == 0) {
        // A thread is telling me to stop sending to it
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->xoff, (void*)sender);
        if (NULL == item) {
            skalXoffItem* xoffItem = SkalMallocZ(sizeof(*xoffItem));
            xoffItem->peer = SkalStrdup(sender);
            bool inserted = CdsMapInsert(priv->xoff,
                    xoffItem->peer, &xoffItem->item);
            SKALASSERT(inserted);
        }

        // Tell originating thread to notify me when I can send again
        SkalMsg* msg2 = SkalMsgCreate("skal-ntf-xon", sender);
        SkalMsgSetIFlags(msg2, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgSend(msg2);

    } else if (strcmp(msgName, "skal-xon") == 0) {
        // A thread is telling me I can resume sending to it
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->xoff, (void*)sender);
        if (NULL == item) {
            // Received an unexpected `xon` message; this can happen in case of
            // retries, just ignore it
        } else {
            CdsMapItemRemove(priv->xoff, item);
        }

    } else if (strcmp(msgName, "skal-ntf-xon") == 0) {
        // A thread is telling me I should notify it when it can send messages
        // again to me
        const char* sender = SkalMsgSender(msg);
        CdsMapItem* item = CdsMapSearch(priv->ntfXon, (void*)sender);
        if (NULL == item) {
            skalNtfXonItem* ntfXonItem = SkalMallocZ(sizeof(*ntfXonItem));
            ntfXonItem->peer = SkalStrdup(sender);
            bool inserted = CdsMapInsert(priv->ntfXon,
                    ntfXonItem->peer, &ntfXonItem->item);
            SKALASSERT(inserted);
        }

    } else if (strcmp(msgName, "skal-terminate") == 0) {
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
        SkalMsg* msg = SkalMsgCreate("skal-xon", ntfXonItem->peer);
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
            SkalMsg* msg = SkalMsgCreate("skal-ntf-xon", xoffItem->peer);
            SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgSend(msg);
            xoffItem->lastNtfXonTime_us = now_us;
        }
    }
}


static void skalMasterThreadRun(void* arg)
{
    skalThreadPrivate* priv = SkalMallocZ(sizeof(*priv));
    priv->thread = gMaster;
    SkalPlfThreadSetSpecific(priv);

    // Tell skald the master thread for this process is starting
    SkalMsg* msg = SkalMsgCreate("skal-init-master-born", "skald");
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
    json[event->in.size_B - 1] = '\0'; // ensure null-termination
    msg = SkalMsgCreateFromJson(json);
    if (NULL == msg) {
        SKALPANIC_MSG("Invalid message received from skald; wrong message format version?");
    }
    SKALASSERT(strcmp(SkalMsgName(msg), "skal-init-domain") == 0);
    SkalSetDomain(SkalMsgGetString(msg, "domain"));
    SkalMsgUnref(msg);
    SkalNetEventUnref(event);

    // Unblock `SkalThreadInit()`
    SkalPlfMutexLock(gMasterRunningMutex);
    gMasterRunning = true;
    SkalPlfMutexUnlock(gMasterRunningMutex);
    msg = SkalMsgCreate("skal-master-init-done", "skal-main");
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
    msg = SkalMsgCreate("skal-master-terminated", "skal-main");
    SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
    SkalQueuePush(gGlobalQueue, msg);
    free(priv);
}


static void skalMasterRouteMsg(SkalMsg* msg)
{
    SkalThread* thread = skalRecipientThread(msg);
    if (NULL == thread) {
        // Recipient is not in this process. This would normally be caught by
        // the local skald, but let's handle it nevertheless.
        if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP) {
            // The sender wants to be notified of dropped message
            SkalMsg* resp = SkalMsgCreate("skal-error-drop",
                    SkalMsgSender(msg));
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(resp, "reason", "no recipient");
            SkalMsgAddFormattedString(resp, "extra",
                    "Thread %s does not exist", SkalMsgRecipient(msg));
            skalSendMsgToSkald(resp);
        }
    } else {
        skalSendMsgToThread(msg, thread);
        skalThreadUnref(thread);
    }
}


static bool skalMasterProcessMsg(SkalMsg* msg)
{
    SKALASSERT(SkalStartsWith(SkalMsgRecipient(msg), "skal-master"));

    bool ok = true;
    const char* msgName = SkalMsgName(msg);
    if (strcmp(msgName, "skal-master-terminate") == 0) {
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
                        thread->cfg.name);
                SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
                SkalQueuePush(thread->queue, msg);
            } // for each thread in this process
        }
        SkalPlfMutexUnlock(gMutex);
        SkalMsgUnref(msg);

    } else if (strcmp(msgName, "skal-terminated") == 0) {
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
