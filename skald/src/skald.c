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
#include <unistd.h>



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
 * NB: Any object of this type is only ever referenced once in the map of the
 * corresponding process. Thus we don't have to keep track of the reference
 * count.
 */
typedef struct {
    CdsMapItem item;
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
} skaldThread;


/** Structure that holds information related to a socket
 *
 * NB: We do not keep track of reference count, because such a structure is only
 * referenced by one and only one skal-net socket.
 */
typedef struct {
    /** What type of socket it is */
    skaldSocketType type;

    /** Name representative of that socket (for debug messages) */
    char name[SKAL_NAME_MAX];

    /** Domain of the peer on the other side of that socket
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_PEER` or
     * `SKALD_SOCKET_FOREIGN_PEER`.
     */
    char domain[SKAL_NAME_MAX];

    /** Map of thread names - made of `skaldNameMapItem`
     *
     * These are the names of the threads that live on the other side of this
     * socket. They may be in a process or in a skald in the same domain.
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_PEER` or
     * `SKALD_SOCKET_PROCESS`; stays empty for all other types.
     */
    CdsMap* threadNames;
} skaldSocketCtx;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** SKALD thread
 *
 * @param arg [in] Thread argument; unused
 */
static void skaldRunThread(void* arg);


/** Get the domain name out of a thread name
 *
 * @param threadName [in] Thread name; must not be NULL
 *
 * @return The domain name, or NULL if no domain name in `threadName`
 */
static const char* skaldDomain(const char* threadName);


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


/** Function to de-reference a `skaldThread`
 *
 * If the last reference is taken out, any thread blocked on the de-referenced
 * thread will be sent a `skal-xon` message to be unblocked.
 */
static void skaldThreadUnref(CdsMapItem* item);


/** Drop a message
 *
 * This function will inform the sender if it requested so. It will also raise
 * an alarm. This function takes ownership of `msg`.
 *
 * @param msg [in,out] Message to drop; must not be NULL
 */
static void skaldDropMsg(SkalMsg* msg);


/** Route a message
 *
 * This function takes ownership of `msg`. It will route the message according
 * to the domain of its recipient.
 *
 * @param msg [in,out] Message to send; must not be NULL
 */
static void skaldRouteMsg(SkalMsg* msg);


/** Send a message through the given socket
 *
 * This function takes ownership of `msg`.
 *
 * @param msg    [in,out] Message to send; must not be NULL
 * @param sockid [in]     On which socket to send this message through
 */
static void skaldMsgSendTo(SkalMsg* msg, int sockid);


/** Take action on an incoming messge from a process
 *
 * This function takes ownership of `msg`.
 *
 * @param sockid [in]     skal-net socket that received this message
 * @param ctx    [in,out] Context of socket that received this message; must not
 *                        be NULL, must be of type SKALD_SOCKET_PROCESS
 * @param msg    [in]     Message to process; must not be NULL
 */
static void skaldHandleMsgFromProcess(int sockid,
        skaldSocketCtx* ctx, SkalMsg* msg);


/** Take action on an incoming message for this skald
 *
 * This function takes ownership of `msg`.
 *
 * @param sockid [in]     skal-net socket that received this message
 * @param ctx    [in,out] Context of socket that received this message; must not
 *                        be NULL, must be of type SKALD_SOCKET_PROCESS
 * @param msg    [in]     Message to process; must not be NULL
 */
static void skaldProcessMsgFromProcess(int sockid,
        skaldSocketCtx* ctx, SkalMsg* msg);



/*------------------+
 | Global variables |
 +------------------*/


/** SKALD thread */
static SkalPlfThread* gSkaldThread = NULL;


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


/** Repeat pipe client sockid for easy access */
static int gPipeClientSockid = -1;


/** Information about all managed and domain threads
 *
 * Map is made of `skaldThread`, and the key is the thread name.
 */
static CdsMap* gThreads = NULL;


/** Alarms currently active
 *
 * Map is made of `skaldAlarm`, and the key is `skaldAlarm->key`.
 */
static CdsMap* gAlarms = NULL;


/** Full name of this skald 'thread' */
static char gName[SKAL_NAME_MAX];



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldRun(const SkaldParams* params)
{
    SKALASSERT(NULL == gSkaldThread);
    SKALASSERT(NULL == gNet);
    SKALASSERT(-1 == gPipeClientSockid);
    SKALASSERT(NULL == gThreads);
    SKALASSERT(NULL == gAlarms);

    SKALASSERT(params != NULL);
    const char* localUrl = params->localUrl;
    if (NULL == localUrl) {
        localUrl = SKAL_DEFAULT_SKALD_URL;
    }

    if (NULL == params->domain) {
        SkalSetDomain("local");
    } else {
        SkalSetDomain(params->domain);
    }
    int n = snprintf(gName, sizeof(gName), "skald@%s", SkalDomain());
    SKALASSERT(n < (int)sizeof(gName));

    gNet = SkalNetCreate(skaldCtxUnref);

    gThreads = CdsMapCreate("threads",         // name
                            0,                 // capacity
                            SkalStringCompare, // compare
                            NULL,              // cookie
                            NULL,              // keyUnref
                            skaldThreadUnref); // itemUnref

    gAlarms = CdsMapCreate("alarms",          // name
                           0,                 // capacity
                           SkalStringCompare, // compare
                           NULL,              // cookie
                           NULL,              // keyUnref
                           skaldAlarmUnref);  // itemUnref

    // Create pipe to allow skald to terminate cleanly
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_SERVER;
    snprintf(ctx->name, sizeof(ctx->name), "pipe-server");
    int sockid = SkalNetServerCreate(gNet, "pipe://", 0, ctx, 0);
    SKALASSERT(sockid >= 0);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(sockid == event->sockid);
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_CLIENT;
    snprintf(ctx->name, sizeof(ctx->name), "pipe-client");
    bool contextSet = SkalNetSetContext(gNet, event->conn.commSockid, ctx);
    gPipeClientSockid = event->conn.commSockid;
    SKALASSERT(contextSet);
    SkalNetEventUnref(event);

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_SERVER;
    snprintf(ctx->name, sizeof(ctx->name), "local-server");
    sockid = SkalNetServerCreate(gNet, localUrl, 0, ctx, 0);
    SKALASSERT(sockid >= 0);

    // Start skald thread
    gSkaldThread = SkalPlfThreadCreate("skald", skaldRunThread, NULL);
}


void SkaldTerminate(void)
{
    char c = 'x';
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gPipeClientSockid, &c, 1);
    SKALASSERT(SKAL_NET_SEND_OK == result);

    // Wait for skald thread to finish
    SkalPlfThreadJoin(gSkaldThread);

    SkalNetDestroy(gNet);
    CdsMapDestroy(gThreads);
    CdsMapDestroy(gAlarms);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skaldRunThread(void* arg)
{
    // Infinite loop: process events on sockets
    bool stop = false;
    while (!stop) {
        SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
        SKALASSERT(event != NULL);

        skaldSocketCtx* ctx = (skaldSocketCtx*)(event->context);
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
                CdsMapClear(ctx->threadNames); // Ensure we don't talk to the dead
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
                    fprintf(stderr, "XXX received >>>%s<<<\n", json);
                    SkalMsg* msg = SkalMsgCreateFromJson(json);
                    if (NULL == msg) {
                        SkalAlarm* alarm = SkalAlarmCreate(
                                "skal-protocol-invalid-json", SKAL_ALARM_ERROR,
                                true, false, "From process '%s'", ctx->name);
                        skaldAlarmProcess(alarm);
                    } else {
                        skaldHandleMsgFromProcess(event->sockid, ctx, msg);
                    }
                }
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on local comm socket",
                        (int)event->type);
                break;
            }
            break;

        default :
            SKALPANIC_MSG("Unexpected socket type %d", (int)ctx->type);
            break;
        }
        SkalNetEventUnref(event);
    } // infinite loop
}


static const char* skaldDomain(const char* threadName)
{
    SKALASSERT(threadName != NULL);
    char* ptr = strchr(threadName, '@');
    if (ptr != NULL) {
        ptr++; // skip '@' character
    }
    return ptr;
}


static void skaldAlarmProcess(SkalAlarm* alarm)
{
    const char* origin = SkalAlarmOrigin(alarm);
    if (NULL == origin) {
        origin = "";
    }
    skaldAlarm* item;
    char key[sizeof(item->key)];
    int n = snprintf(key, sizeof(key), "%s#%s", origin, SkalAlarmName(alarm));
    SKALASSERT(n < (int)sizeof(key));

    if (SkalAlarmIsOn(alarm)) {
        item = SkalMallocZ(sizeof(*item));
        snprintf(item->key, sizeof(item->key), "%s", key);
        item->alarm = alarm;
        bool inserted = CdsMapInsert(gAlarms, item->key, &item->item);
        SKALASSERT(inserted);
    } else {
        (void)CdsMapRemove(gAlarms, key);
        SkalAlarmUnref(alarm);
    }
}


static void skaldNameMapItemUnref(CdsMapItem* item)
{
    free(item);
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
    ctx->threadNames = CdsMapCreate(NULL,                   // name
                                    0,                      // capacity
                                    SkalStringCompare,      // compare
                                    NULL,                   // cookie
                                    NULL,                   // keyUnref
                                    skaldNameMapItemUnref); // itemUnref
    bool contextSet = SkalNetSetContext(gNet, commSockid, ctx);
    SKALASSERT(contextSet);
}


static void skaldThreadUnref(CdsMapItem* item)
{
    skaldThread* thread = (skaldThread*)item;
    SKALASSERT(thread != NULL);
    free(thread);
}


static void skaldHandleMsgFromProcess(int sockid,
        skaldSocketCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    SKALASSERT(msg != NULL);

    // Basic checks on `msg`
    const char* msgName = SkalMsgName(msg);
    SKALASSERT(msgName != NULL);
    const char* sender = SkalMsgSender(msg);
    if (NULL == skaldDomain(sender)) {
        SkalAlarm* alarm = SkalAlarmCreate(
                "skal-protocol-sender-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the sender has no domain: '%s' (message name: '%s')",
                sender,
                msgName);
        skaldAlarmProcess(alarm);
        SkalMsgUnref(msg);
        return;
    }
    const char* recipient = SkalMsgRecipient(msg);
    if (NULL == skaldDomain(recipient)) {
        SkalAlarm* alarm = SkalAlarmCreate(
                "skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the recipient has no domain: '%s' (message name: '%s')",
                recipient,
                msgName);
        skaldAlarmProcess(alarm);
        SkalMsgUnref(msg);
        return;
    }

    if (    (strcmp(recipient, gName) == 0)
         || (strncmp(msgName, "skal-init-", 10) == 0)) {
        // The recipient of this message is this skald.
        //
        // Please note if the message name starts with "skal-init-", it is part
        // of the SKAL protocol where a process connects to us. So such a
        // message is always for the skald which is local to the process, even
        // if the recipient domain is not set correctly (because the process
        // doesn't know yet what is its domain).
        fprintf(stderr, "XXX %s: calling skaldProcessMsgFromProcess()\n", __func__);
        skaldProcessMsgFromProcess(sockid, ctx, msg);
        return;
    }

    // Special treatment for `skal-ntf-xon` messages: ensure `sender` is not
    // blocked on a non-existing `recipient`.
    if (strcmp(msgName, "skal-ntf-xon") == 0) {
        const char* domain = skaldDomain(recipient);
        SKALASSERT(domain != NULL);
        if (    (strcmp(domain, SkalDomain()) == 0)
             && (CdsMapSearch(gThreads, (void*)recipient) == NULL)) {
            // Unblock `sender` and don't forward the original msg
            SkalMsg* resp = SkalMsgCreate("skal-xon", sender, 0, NULL);
            SkalMsgSetSender(resp, recipient);
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            skaldRouteMsg(resp);
            SkalMsgUnref(msg);
            return;
        }
        // else: The recipient is on a different domain, just forward the
        // message to the appropriate skald.
    }

    fprintf(stderr, "XXX %s: calling skaldRouteMsg()\n", __func__);
    skaldRouteMsg(msg);
}


static void skaldProcessMsgFromProcess(int sockid,
        skaldSocketCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    SKALASSERT(msg != NULL);

    // Take action depending on message name
    const char* msgName = SkalMsgName(msg);
    const char* sender = SkalMsgSender(msg);
    if (strcmp(msgName, "skal-init-master-born") == 0) {
        // The `skal-master` thread of a process is uttering its first words!
        if (!SkalMsgHasField(msg, "name")) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-protocol-missing-field",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-init-master-born' message from '%s' without a 'name' field",
                    sender);
            skaldAlarmProcess(alarm);

        } else {
            const char* name = SkalMsgGetString(msg, "name");
            snprintf(ctx->name, sizeof(ctx->name), "%s", name);

            // Respond with the domain name
            SkalMsg* resp = SkalMsgCreate("skal-init-domain",
                    "skal-master", 0, NULL);
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(resp, "domain", SkalDomain());
            skaldMsgSendTo(resp, sockid);
        }

    } else if (strcmp(msgName, "skal-born") == 0) {
        // A managed or domain thread has been born
        const char* domain = skaldDomain(sender);
        SKALASSERT(domain != NULL);
        if (strcmp(domain, SkalDomain()) != 0) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-protocol-wrong-sender-domain",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', which is on a different domain than mine (%s)",
                    sender, SkalDomain());
            skaldAlarmProcess(alarm);

        } else if (CdsMapSearch(gThreads, (void*)sender) != NULL) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-conflict-duplicate-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', but a thread with that name is already registered",
                    sender);
            skaldAlarmProcess(alarm);

        } else if (CdsMapSearch(ctx->threadNames, (void*)sender) != NULL) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' not registered globally, but is listed for process '%s'; this is impossible",
                    sender, ctx->name);
            skaldAlarmProcess(alarm);

        } else {
            skaldThread* thread = SkalMallocZ(sizeof(*thread));
            thread->sockid = sockid;
            snprintf(thread->name, sizeof(thread->name), "%s", sender);
            bool inserted = CdsMapInsert(gThreads, (void*)sender, &thread->item);
            SKALASSERT(inserted);

            skaldNameMapItem* item = SkalMallocZ(sizeof(*item));
            snprintf(item->name, sizeof(item->name), "%s", sender);
            inserted = CdsMapInsert(ctx->threadNames,
                    (void*)(item->name), &item->item);
            SKALASSERT(inserted);

            if (SKALD_SOCKET_PROCESS == ctx->type) {
                // TODO: inform all domain peers
            }
        }

    } else if (strcmp(msgName, "skal-died") == 0) {
        // A managed or domain thread just died
        bool removed = CdsMapRemove(gThreads, (void*)sender);
        if (!removed) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-conflict-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received 'skal-died' for unknown thread '%s'",
                    sender);
            skaldAlarmProcess(alarm);
        }
        removed = CdsMapRemove(ctx->threadNames, (void*)sender);
        if (!removed) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' is registered globally, but not for process '%s'; this is impossible",
                    sender, ctx->name);
            skaldAlarmProcess(alarm);
        }

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform domain peers
        }

    } else if (strcmp(msgName, "skal-ping") == 0) {
        SkalMsg* resp = SkalMsgCreate("skal-pong", sender, 0, NULL);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        skaldRouteMsg(resp);

    } else {
        SkalAlarm* alarm = SkalAlarmCreate("skal-protocol-unknown-message",
                SKAL_ALARM_NOTICE, true, false,
                "Received unknown message '%s' from '%s'",
                msgName, sender);
        skaldAlarmProcess(alarm);
    }

    SkalMsgUnref(msg);
}


static void skaldDropMsg(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);

    SkalAlarm* alarm = SkalAlarmCreate("skal-drop-no-recipient",
            SKAL_ALARM_WARNING, true, false,
            "Can't route message '%s' because I know nothing about its recipient '%s'; message dropped",
            SkalMsgName(msg), SkalMsgRecipient(msg));
    skaldAlarmProcess(alarm);

    if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP) {
        char* XXX = SkalMsgToJson(msg);
        fprintf(stderr, "XXX %s: drop msg & notify sender >>>%s<<<\n", __func__, XXX);
        free(XXX);
        SkalMsg* resp = SkalMsgCreate("skal-error-drop",
                SkalMsgSender(msg), 0, NULL);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(resp, "original-marker", SkalMsgMarker(msg));
        SkalMsgAddString(resp, "reason", "no-recipient");
        SkalMsgAddFormattedString(resp, "extra",
                "Thread '%s' does not exist", SkalMsgRecipient(msg));
        skaldRouteMsg(resp);
    }

    SkalMsgUnref(msg);
}


static void skaldRouteMsg(SkalMsg* msg)
{
    const char* recipient = SkalMsgRecipient(msg);
    const char* domain = skaldDomain(recipient);
    char* XXX = SkalMsgToJson(msg);
    fprintf(stderr, "XXX %s: domain=%s >>>%s<<<\n", __func__, domain, XXX);
    free(XXX);

    if (NULL == domain) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Can't route message '%s': recipient has no domain: '%s'",
                SkalMsgName(msg), recipient);
        skaldAlarmProcess(alarm);
        SkalMsgUnref(msg);

    } else if (strcmp(domain, SkalDomain()) == 0) {
        skaldThread* thread = (skaldThread*)CdsMapSearch(gThreads,
                (void*)recipient);
        fprintf(stderr, "XXX %s: searching for recipient thread '%s', found %p\n", __func__, recipient, thread);
        if (thread != NULL) {
            skaldMsgSendTo(msg, thread->sockid);
        } else if (strcmp(recipient, gName) == 0) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-conflict-circular-msg",
                    SKAL_ALARM_WARNING, true, false,
                    "Can't route message '%s': recipient '%s' is myself",
                    SkalMsgName(msg), recipient);
            skaldAlarmProcess(alarm);
        } else {
            fprintf(stderr, "XXX %s: calling skaldDropMsg()\n", __func__);
            skaldDropMsg(msg);
        }

    } else {
        // The recipient is in another domain
        //  => Look up foreign skald to send to
        // TODO
        char* json = SkalMsgToJson(msg);
        fprintf(stderr, "XXX KAPUT MSG >>>%s<<<\n", json);
        free(json);
        SKALPANIC_MSG("Not yet implemented: message domain %s", domain);
    }
}


static void skaldMsgSendTo(SkalMsg* msg, int sockid)
{
    SKALASSERT(msg != NULL);
    skaldSocketCtx* ctx = (skaldSocketCtx*)SkalNetGetContext(gNet, sockid);
    SKALASSERT(ctx != NULL);

    switch (ctx->type) {
    case SKALD_SOCKET_DOMAIN_PEER :
    case SKALD_SOCKET_FOREIGN_PEER :
        SKALPANIC_MSG("Peer comms not yet implemented");
        break;
    case SKALD_SOCKET_PROCESS :
        {
            char* json = SkalMsgToJson(msg);
            fprintf(stderr, "XXX %s: >>>%s<<<\n", __func__, json);
            SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, sockid,
                    json, strlen(json) + 1);
            free(json);
            if (result != SKAL_NET_SEND_OK) {
                SkalAlarm* alarm = SkalAlarmCreate("skal-io-send-fail",
                        SKAL_ALARM_ERROR, true, false,
                        "Failed to send over socket '%s'", ctx->name);
                skaldAlarmProcess(alarm);
                SkalNetSocketDestroy(gNet, sockid);
            }
        }
        break;
    default :
        SKALPANIC_MSG("Can't send a msg over socket type %d", (int)ctx->type);
    }
    SkalMsgUnref(msg);
}
