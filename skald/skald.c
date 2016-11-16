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

#include "skald.h"
#include "skal.h"
#include "skal-msg.h"
#include "skal-net.h"
#include "cdsmap.h"
#include <stdlib.h>
#include <string.h>



/*----------------+
 | Macros & Types |
 +----------------*/


/** Item of the alarm map
 *
 * We don't keep track of the reference count because this item is only
 * ever referenced once by the `gAlarms` map.
 */
typedef struct {
    CdsMapItem item;
    char       key[SKAL_NAME_MAX * 2]; /**< "alarm-type#alarm-origin" */
    SkalAlarm* alarm;
} skaldAlarm;


/** The different types of sockets */
typedef enum {
    /** Pipe server - to allow skald to terminate itself cleanly */
    SKALD_SOCKET_PIPE_SERVER,

    /** Pipe client - to tell skald to terminate itself */
    SKALD_SOCKET_PIPE_CLIENT,

    /** Domain peers - other skald's in the same domain */
    SKALD_SOCKET_DOMAIN_PEER,

    /** Foreign peers - skald's in other domains (only for the gateway) */
    SKALD_SOCKET_FOREIGN_PEER,

    /** Local server - for processes to connect to me */
    SKALD_SOCKET_SERVER,

    /** Local comm - one per process */
    SKALD_SOCKET_PROCESS
} skaldSocketType;


/** Structure that represents a map item that only contains a name
 *
 * This is used in various places to keep track of the names of certain threads.
 */
typedef struct {
    CdsMapItem item;
    int        ref;
    char       name[SKAL_NAME_MAX];
} skaldNameMapItem;


/** Structure that holds information about a managed or domain thread
 *
 * This goes into the `gThreads` map.
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsMapItem item;

    /** Socket id of the process/skald this thread is part of */
    int sockid;

    /** Name of the thread in question - this is also the map key */
    char name[SKAL_NAME_MAX];

    /** Map of threads blocked by this thread; made of `skaldNameMapItem` */
    CdsMap* blockedByMe;

    /** Map of threads blocking this thread; made of `skaldNameMapItem` */
    CdsMap* blockingMe;
} skaldThread;


/** Structure that holds information related to a socket
 *
 * NB: We do not keep track of reference count, because such a structure is only
 * referenced by one and only one skal-net socket.
 */
typedef struct {
    /** What type of socket it is */
    skaldSocketType type;

    /** Name representative of that socket */
    char name[SKAL_NAME_MAX];

    /** Map of threads - made of `skaldNameMapItem`
     *
     * These are the threads that live on the other side of this socket. They
     * may be in a process or in a skald in the same domain.
     *
     * This map remains empty for a foreign peer.
     */
    CdsMap* threadNames;
} skaldSocketCtx;


/** Structure that holds information about a route
 *
 * This goes into the `gRoutingTable` map.
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsMapItem item;

    /** Name of the domain in question - this is also the map key */
    char domain[SKAL_NAME_MAX];

    /** Socket id of gateway
     *
     * All messages where the recipient is in the `domain` listed above will be
     * sent through this socket.
     */
    int sockid;
} skaldRoute;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Get the domain name out of a thread name
 *
 * @param threadName [in] Thread name; must not be NULL
 *
 * @return The domain name, or NULL if no domain name
 */
static const char* skaldDomain(const char* threadName);


/** Insert a name in a map */
static void skaldInsertName(CdsMap* map, const char* name);


/** Insert/remove an alarm from the alarm map
 *
 * The action (insert vs remove) depends on whether the `alarm` is on or off.
 *
 * The ownership of `alarm` is transferred to this function.
 *
 * @param alarm [in] Alarm to process; must not be NULL
 */
static void skaldAlarmProcess(SkalAlarm* alarm);


/** De-reference a name item */
static void skaldNameMapItemUnref(CdsMapItem* item);


/** De-reference an item from the alarm map */
static void skaldAlarmUnref(CdsMapItem* item);


/** Function to de-reference a skal-net socket context */
static void skaldCtxUnref(void* context);


/** Create a new process following a connection request
 *
 * @param commSockid [in] Socket id of new process
 */
static void skaldHandleProcessConnection(int commSockid);


/** Handle the disconnection of a process
 *
 * The main task of skald when a process disconnects is to essentially act as if
 * all threads in this process died. In particular it must wake up other threads
 * currently blocked in any thread of the crashed process.
 *
 * @param ctx [in,out] Context for that socket; must not be NULL; must be of
 *                     type `SKALD_SOCKET_PROCESS`
 */
static void skaldHandleProcessDisconnection(skaldSocketCtx* ctx);


/** Handle the death of a managed or domain thread
 *
 * This function does the following:
 *  - Unblock any thread still blocked on it by an XOFF
 *  - Remove all reference to that thread from its parent process/peer and other
 *    threads
 *
 * In most likelyhood, `thread` will be freed in the course of this function.
 *
 * @param threadName [in] Name of thread that just died; must not be NULL
 */
static void skaldHandleThreadDeath(const char* threadName);


/** Function to de-reference a `skaldThread`
 *
 * If the last reference is taken out, any thread blocked on the de-referenced
 * thread will be sent a `skal-xon` message to be unblocked.
 */
static void skaldThreadUnref(CdsMapItem* item);


/** Helper function to unblock all threads blocked by the given thread
 *
 * This function sends `skal-xon` messages to all threads currently blocked by
 * the given `thread`.
 *
 * @param thread [in] Blocking thread; must not be NULL
 */
static void skaldThreadSendXon(const skaldThread* thread);


/** Send a message through the given socket
 *
 * This function takes ownership of `msg`.
 *
 * @param msg    [in,out] Message to send; must not be NULL
 * @param sockid [in]     On which socket to send this message through
 */
static void skaldMsgSend(SkalMsg* msg, int sockid);


/** Handle error condition where a message is received from an unexpected socket
 *
 * This function raises the appropriate alarm and closes the `sockid` socket.
 *
 * @param sockid  [in] Id of socket that received the message
 * @param ctx     [in] Context of same socket
 * @param msgtype [in] Type of received message
 * @param extra   [in] Extra message (goes in alarm message)
 */
static void skaldWrongSocketType(int sockid, const skaldSocketCtx* ctx,
        const char* msgtype, const char* extra);


/** Process an incoming messge
 *
 * @param sockid [in]     skal-net socket that received this message
 * @param ctx    [in,out] Context of socket that received this message
 * @param msg    [in]     Message to process; must not be NULL
 */
static void skaldHandleMsg(int sockid, skaldSocketCtx* ctx, const SkalMsg* msg);



/*------------------+
 | Global variables |
 +------------------*/


/** Sockets
 *
 * There are 3 types of sockets:
 *  - A pipe that is used to terminate skald (eg: SIGTERM, etc.)
 *  - Sockets that link to a process on this machine
 *  - Sockets that link to another skald
 *
 * We use the skal-net ability to hold cookies for each socket to store
 * information related to each one of those sockets; cookie are thus of type
 * `skaldSocketCtx`.
 */
static SkalNet* gNet = NULL;


/** Repeat pipe server sockid for easy termination */
static int gPipeServerSockid = -1;


/** Information about all managed and domain threads
 *
 * Map is made of `skaldThread`, and the key is the thread name.
 */
static CdsMap* gThreads = NULL;


/** Alarms currently active */
static CdsMap* gAlarms = NULL;


/** Domain this skald manages */
static char gDomain[SKAL_DOMAIN_NAME_MAX] = "local";


/** Routing table
 *
 * If this skald is the designated gateway for its domain, this table lists all
 * the other getways for other domains. Items are of type `skaldRoute`.
 *
 * If this skald is not a designated gateway, this table remains empty.
 */
static CdsMap* gRoutingTable = NULL;


/** Is this skald the gateway for this domain? */
static bool gIsGateway = false;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldRun(const SkaldParams* params)
{
    SKALASSERT(params != NULL);
    if (params->domain != NULL) {
        SKALASSERT(SkalIsAsciiString(params->domain, SKAL_DOMAIN_NAME_MAX));
        int n = snprintf(gDomain, sizeof(gDomain), "%s", params->domain);
        SKALASSERT(n < (int)sizeof(gDomain));
    }
    SKALASSERT(params->localAddr.unix.path[0] != '\0');
    SKALASSERT(SkalIsAsciiString(params->localAddr.unix.path,
                sizeof(params->localAddr.unix.path)));

    gNet = SkalNetCreate(params->pollTimeout_us, skaldCtxUnref);
    gIsGateway = params->isGateway;

    gAlarms = CdsMapCreate( "alarms",           // name
                            0,                  // capacity
                            SkalStringCompare,  // compare
                            NULL,               // cookie
                            NULL,               // keyUnref
                            skaldAlarmUnref);   // itemUnref

    // Create pipe to allow skald to terminate cleanly
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_SERVER;
    snprintf(ctx->name, sizeof(ctx->name), "pipe-server");
    int sockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_PIPE, NULL, 0, ctx, 0);
    gPipeServerSockid = sockid;
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(sockid == event->sockid);
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_CLIENT;
    snprintf(ctx->name, sizeof(ctx->name), "pipe-client");
    bool contextSet = SkalNetSetContext(gNet, event->conn.commSockid, ctx);
    SKALASSERT(contextSet);
    SkalNetEventUnref(event);

    // TODO: Connect to other skald's
    (void)peers;
    (void)npeers;

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_SERVER;
    snprintf(ctx->name, sizeof(ctx->name), "local-server");
    (void)SkalNetServerCreate(gNet,
            SKAL_NET_TYPE_UNIX_SEQPACKET, &localAddr, 0, ctx, 0);

    // Infinite loop: process events on sockets
    bool stop = false;
    while (!stop) {
        event = SkalNetPoll_BLOCKING(gNet);
        if (NULL == event) {
            // Poll timeout without anything happening
            continue;
        }

        ctx = (skaldSocketCtx*)(event->context);
        SKALASSERT(ctx != NULL);

        switch (ctx->type) {
        case SKALD_SOCKET_PIPE_SERVER :
            switch (event->type) {
            case SKAL_NET_EV_IN :
                stop = true;
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on pipe server",
                        (int)event->type);
                break;
            }
            break;

        case SKALD_SOCKET_PIPE_CLIENT :
            SKALPANIC_MSG("Unexpected event %d on pipe client",
                    (int)event->type);
            break;

        case SKALD_SOCKET_DOMAIN_PEER :
        case SKALD_SOCKET_FOREIGN_PEER :
            SKALPANIC_MSG("Not yet implemented");
            break;

        case SKALD_SOCKET_SERVER :
            switch (event->type) {
            case SKAL_NET_EV_CONN :
                // A process is connecting to us
                skaldHandleProcessConnection(event->conn.commSockid);
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on local server socket",
                        (int)event->type);
                break;
            }
            break;

        case SKALD_SOCKET_PROCESS :
            switch (event->type) {
            case SKAL_NET_EV_ERROR :
                SkalLog("SKALD: Error reported on socket '%s'", ctx->name);
                // fallthrough
            case SKAL_NET_EV_DISCONN :
                // This process is disconnecting from us
                skaldHandleProcessDisconnection(ctx);
                SkalNetSocketDestroy(gNet, event->sockid);
                break;
            case SKAL_NET_EV_IN :
                {
                    SKALASSERT(event->in.data != NULL);
                    SKALASSERT(event->in.size_B > 0);
                    char* json = (char*)(event->in.data);
                    // The string should be null-terminated, but it's safer to
                    // enforce null termination
                    json[event->in.size_B - 1] = '\0';
                    SkalMsg* msg = SkalMsgCreateFromJson(json);
                    if (NULL == msg) {
                        SkalAlarm* alarm = SkalAlarmCreate(
                                "skal-invalid-json", SKAL_SEVERITY_ERROR,
                                true, false, "From process '%s'", ctx->name);
                        skaldAlarmProcess(alarm);
                    } else {
                        skaldHandleMsg(event->sockid, ctx, msg);
                        SkalMsgUnref(msg);
                    }
                }
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on local comm socket",
                        (int)event->type);
                break;
            }
        default :
            SKALPANIC_MSG("Unexpected socket type %d", (int)ctx->type);
            break;
        }
        SkalNetEventUnref(event);
    } // infinite loop

    SkalNetDestroy(gNet);
    CdsMapDestroy(gAlarm);
}


static void SkaldTerminate(void)
{
    char c = 'x';
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gPipeServerSockid, &c, 1);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static const char* skaldDomain(const char* threadName)
{
    SKALASSERT(threadName != NULL);
    char* ptr = strchr(threadName, '@');
    if (ptr != NULL) {
        ptr++; // skip '@' character
    }
    return ptr;
}


static void skaldInsertName(CdsMap* map, const char* name)
{
    SKALASSERT(map != NULL);
    SKALASSERT(SkalIsUtf8String(name, SKAL_NAME_MAX));

    skaldNameMapItem* item = SkalMallocZ(sizeof(*item));
    item->ref = 1;
    snprintf(item->name, sizeof(item->name), "%s", name);

    bool inserted = CdsMapInsert(map, &item->item);
    SKALASSERT(inserted);
}


static void skaldAlarmProcess(SkalAlarm* alarm)
{
    skaldAlarm* item = SkalMallocZ(sizeof(*item));
    const char* origin = SkalAlarmOrigin(alarm);
    if (NULL == origin) {
        origin = "";
    }
    snprintf(item->key, sizeof(item->key),
            "%s#%s", origin, SkalAlarmType(alarm));
    item->alarm = alarm;

    if (SkalAlarmIsOn(alarm)) {
        bool inserted = CdsMapInsert(gAlarms, item->key, &item->item);
        SKALASSERT(inserted);
    } else {
        (void)CdsMapRemove(gAlarms, item->key);
        skaldAlarmUnref(&item->item);
    }
}


static void skaldNameMapItemUnref(CdsMapItem* item)
{
    skaldNameMapItem* n = (skaldNameMapItem*)item;
    SKALASSERT(n != NULL);
    (n->ref)--;
    if (n->ref <= 0) {
        free(n);
    }
}


static void skaldAlarmUnref(CdsMapItem* item)
{
    skaldAlarm* alarmItem = (skaldAlarm*)item;
    SKALASSERT(alarmItem != NULL);
    SKALASSERT(alarmItem->alarm != NULL);
    SkalAlarmUnref(alarmItem->alarm);
    free(alarmItem);
}


static void skaldCtxUnref(void* context)
{
    SKALASSERT(context != NULL);
    skaldSocketCtx* ctx = (skaldSocketCtx*)context;
    if (ctx->threadNames != NULL) {
        CdsMapDestroy(ctx->threadNames);
    }
    free(ctx);
}


static void skaldHandleProcessConnection(int commSockid)
{
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PROCESS;
    snprintf(ctx->name, sizeof(ctx->name), "process (%d)", commSockid);
    ctx->threads = CdsMapCreate(NULL,                   // name
                                0,                      // capacity
                                SkalStringCompare,      // compare
                                NULL,                   // cookie
                                NULL,                   // keyUnref
                                skaldNameMapItemUnref); // itemUnref
    bool contextSet = SkalNetSetContext(gNet, commSockid, ctx);
    SKALASSERT(contextSet);
}


static void skaldHandleProcessDisconnection(skaldSocketCtx* ctx)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    SKALASSERT(ctx->threadNames != NULL);

    CdsMapIteratorReset(ctx->threadNames, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(ctx->threadNames, NULL);
            item != NULL;
            item = CdsMapIteratorNext(ctx->threadNames, NULL)) {
        skaldNameMapItem* nameitem = (skaldNameMapItem*)item;
        skaldHandleThreadDeath(nameitem->name);
    }
    SKALASSERT(CdsMapIsEmpty(ctx->threadNames));
}


static void skaldHandleThreadDeath(const char* threadName)
{
    // Look up thread structure based on thread name
    SKALASSERT(threadName != NULL);
    skaldThread* thread = (skaldThread*)CdsMapSearch(gThreads, threadName);
    if (NULL == thread) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-unknown-thread",
                SKAL_SEVERITY_WARNING, true, false, NULL);
        skaldAlarmProcess(alarm);
        return;
    }

    // Unblock any thread still blocked by the thread that just died
    CdsMapIteratorReset(thread->blockedByMe, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(thread->blockedByMe, NULL);
            item != NULL;
            item = CdsMapIteratorNext(thread->blockedByMe, NULL)) {
        skaldNameMapItem* nameitem = (skaldNameMapItem*)item;
        skaldThread* blocked = CdsMapSearch(gThreads, nameitem->name);
        if (NULL == blocked) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-bug-blockedbyme",
                    SKAL_SEVERITY_WARNING, true, false,
                    "Thread '%s' says it blocks '%s', but '%s' does not exist",
                    threadName, nameitem->name, nameitem->name);
            skaldAlarmProcess(alarm);
        } else {
            SkalMsg* msg = SkalMsgCreate("skal-xon", nameitem->name, 0, NULL);
            SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgSetSender(msg, threadName);
            skaldMsgSend(msg, thread->sockid);
        }
    }
    CdsMapClear(thread->blockedbyme);

    // Remove any reference to `thread` from other threads
    CdsMapIteratorReset(thread->blockingMe, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(thread->blockingMe, NULL);
            item != NULL;
            item = CdsMapIteratorNext(thread->blockingMe, NULL)) {
        skaldNameMapItem* nameitem = (skaldNameMapItem*)item;
        skaldThread* blocking = CdsMapSearch(gThreads, nameitem->name);
        if (NULL == blocking) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-bug-blockingme",
                    SKAL_SEVERITY_WARNING, true, false,
                    "Thread '%s' says it is blocked by '%s', but '%s' does not exist",
                    threadName, nameitem->name, nameitem->name);
            skaldAlarmProcess(alarm);
        } else {
            bool removed = CdsMapRemove(blocking->blockedByMe, threadName);
            if (!removed) {
                SkalAlarm* alarm = SkalAlarmCreate("skal-bug-blockedbyme",
                        SKAL_SEVERITY_WARNING, true, false,
                        "Thread '%s' says it is blocked by '%s', but thread '%s' does not have a record of that"
                        threadName, nameitem->name, nameitem->name);
                skaldAlarmProcess(alarm);
            }
        }
    }
    CdsMapClear(thread->blockingMe);

    // Remove any reference from the process/skald structure
    skaldSocketCtx* ctx = (skaldSocketCtx*)SkalNetGetContext(gNet,
            thread->sockid);
    SKALASSERT(ctx != NULL);
    switch (ctx->type) {
    SKALASSERT(    (SKALD_SOCKET_DOMAIN_PEER == ctx->type)
                || (SKALD_SOCKET_PROCESS == ctx->type));
    bool removed2 = CdsMapRemove(ctx->threadNames, threadName);
    if (!removed2) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-bug-unknown-thread",
                SKAL_SEVERITY_WARNING, true, false,
                "Thread '%s' should be part of '%s', but it's not listed there...",
                threadName, ctx->name);
        skaldAlarmProcess(alarm);
    }
}


static void skaldThreadUnref(CdsMapItem* item)
{
    skaldThread* thread = (skaldThread*)item;
    SKALASSERT(thread != NULL);
    SKALASSERT(CdsMapIsEmpty(thread->blockedByMe));
    SKALASSERT(CdsMapIsEmpty(thread->blockingMe));
    CdsMapDestroy(thread->blockedByMe);
    CdsMapDestroy(thread->blockingMe);
    free(thread);
}


static const char* skaldSocketTypeToString(skaldSocketType type)
{
    const char* str = "UNKNOWN";
    switch (type) {
        case SKALD_SOCKET_PIPE_SERVER : str = "PIPE_SERVER"; break;
        case SKALD_SOCKET_PIPE_CLIENT : str = "PIPE_CLIENT"; break;
        case SKALD_SOCKET_PEER        : str = "PEER"       ; break;
        case SKALD_SOCKET_SERVER      : str = "SERVER"     ; break;
        case SKALD_SOCKET_PROCESS     : str = "PROCESS"    ; break;
        default :
    }
    return str;
}


static void skaldWrongSocketType(int sockid, const skaldSocketCtx* ctx,
        const char* msgtype, const char* extra)
{
    SkalAlarm* alarm = SkalAlarmCreate(
            "skal-wrong-socket-type",
            SKAL_SEVERITY_ERROR,
            true,
            false,
            "From socket '%s' (type %s) for msg '%s' - closing socket%s%s",
            ctx->name,
            skaldSocketTypeToString(ctx->type),
            msgtype,
            (extra != NULL) ? "; " : "",
            (extra != NULL) ? extra : "");
    skaldAlarmProcess(alarm);
    SkalNetSocketDestroy(gNet, sockid);
}


static void skaldHandleMsg(int sockid, skaldSocketCtx* ctx, const SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(msg != NULL);

    const char* type = SkalMsgType(msg);
    if (strcmp(type, "skal-master-born") == 0) {
        // The `skal-master` thread of a process provides us with information
        if (ctx->type != SKALD_SOCKET_PROCESS) {
            skaldWrongSocketType(sockid, ctx,
                    type, "expected SKALD_SOCKET_PROCESS");
            return;
        }
        const char* name = SkalMsgGetString(msg, "name");
        snprintf(ctx->name, sizeof(ctx->name), "%s", name);

        // Send a `skal-domain` message in response
        SkalMsg* resp = SkalMsgCreate("skal-domain", "skal-master", 0, NULL);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(resp, "domain", gDomain);
        skaldMsgSend(msg, sockid);

    } else if (strcmp(type, "skal-born") == 0) {
        // A managed or domain thread has been born
        if (    (ctx->type != SKALD_SOCKET_PROCESS)
             || (ctx->type != SKALD_SOCKET_DOMAIN_PEER)) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS or SKALD_SOCKET_DOMAIN_PEER");
            return;
        }

        skaldThread* thread = SkalMallocZ(sizeof(*thread));
        thread->sockid = sockid;

        const char* sender = SkalMsgSender(msg);
        if (SKALD_SOCKET_PROCESS == ctx->type) {
            char* threadDomain = skaldDomain(sender);
            SKALASSERT(threadDomain != NULL);
            SKALASSERT(strcmp(threadDomain, gDomain) == 0);
        }
        snprintf(thread->name, sizeof(thread->name), "%s", sender);

        thread->blockedByMe = CdsMapCreate(NULL,                   // name
                                           0,                      // capacity
                                           SkalStringCompare,      // compare
                                           NULL,                   // cookie
                                           NULL,                   // keyUnref
                                           skaldNameMapItemUnref); // itemUnref

        thread->blockingMe = CdsMapCreate(NULL,                   // name
                                          0,                      // capacity
                                          SkalStringCompare,      // compare
                                          NULL,                   // cookie
                                          NULL,                   // keyUnref
                                          skaldNameMapItemUnref); // itemUnref

        bool inserted = CdsMapInsert(gThreads, (void*)sender, &thread->item);
        SKALASSERT(inserted);

        skaldInsertName(ctx->threadNames, sender);

        // TODO: if managed thread, inform domain peers

    } else if (strcmp(type, "skal-died") == 0) {
        // A managed or domain thread just died
        if (    (ctx->type != SKALD_SOCKET_PROCESS)
             || (ctx->type != SKALD_SOCKET_DOMAIN_PEER)) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS or SKALD_SOCKET_DOMAIN_PEER");
            return;
        }
        skaldHandleThreadDeath(SkalMsgSender(msg));

        // TODO: if managed thread, inform domain peers

    } else if (strcmp(type, "skal-xoff") == 0) {
        // A thread is telling another thread to stop sending to it
        if (    (ctx->type != SKALD_SOCKET_PROCESS)
             || (ctx->type != SKALD_SOCKET_DOMAIN_PEER)) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS or SKALD_SOCKET_DOMAIN_PEER");
            return;
        }

        // We only do something if the recipient is managed by this skald
        const char* recipient = SkalMsgRecipient(msg);
        const char* recipientDomain = skaldDomain(recipient);
        SKALASSERT(recipientDomain != NULL);
        if (strcmp(recipientDomain, gDomain) == 0) {
            skaldThread* blocked = CdsMapSearch(gThreads, recipient);
            if (blocked != NULL) {
                const char* sender = SkalMsgSender(msg);
                skaldInsertName(blocked->blockingMe, sender);
                // TODO: from here
        }
        skaldThread* blocking = CdsMapSearch(gThreads, sender);
        if (blocking != NULL) {
            skaldInsertName(blocking->blockedByMe, recipient);
        }

    } else if (strcmp(type, "skal-ntf-xon") == 0) {
        // TODO

    } else if (strcmp(type, "skal-xon") == 0) {
        // TODO
    }
}


static void skaldSendMsgOverCnxSocket(SkalMsg* msg,
        int sockid, skaldSocketCtx* ctx)
{
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    skaldProcess* process = &ctx->process;
    if (ctx->unwell) {
        SkalLog("SKALD: Not sending message over socket '%s' because it is unwell",
                ctx->name);
        return;
    }

    char* json = SkalMsgToJson(msg);
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, sockid,
            json, strlen(json) + 1);
    free(json);
    if (result != SKAL_NET_SEND_OK) {
        ctx->unwell = true;
        SkalAlarm* alarm = SkalAlarmCreate("skal-send-fail",
                SKAL_SEVERITY_ERROR, true, false,
                "Over socket '%s'", ctx->name);
        SkalNetSocketDestroy(gNet, sockid);
    }
}

static void skaldMsgSend(SkalMsg* msg, int sockid)
{
    SKALASSERT(msg != NULL);
    skaldSocketCtx* ctx = (skaldSocketCtx*)SkalNetGetContext(gNet,
            blocked->sockid);
    SKALASSERT(ctx != NULL);

    switch (ctx->type) {
    case SKALD_SOCKET_PEER :
        SKALPANIC_MSG("Peer comms not yet implemented");
        break;
    case SKALD_SOCKET_PROCESS :
        skaldSendMsgOverCnxSocket(msg, sockid, ctx);
        break;
    default :
        SKALPANIC_MSG("Can't send a msg over socket type %d", (int)ctx->type);
    }
    SkalMsgUnref(msg);
}
