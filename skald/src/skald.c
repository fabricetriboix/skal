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


/** Structure that represents a map item that only contains a name */
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

    /** Domain of the peer on the other side of that socket
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_PEER` or
     * `SKALD_SOCKET_FOREIGN_PEER`.
     */
    char domain[SKAL_NAME_MAX];

    /** Map of threads - made of `skaldNameMapItem`
     *
     * These are the threads that live on the other side of this socket. They
     * may be in a process or in a skald in the same domain.
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_PEER` or
     * `SKALD_SOCKET_PROCESS`; stays empty for all other types.
     */
    CdsMap* threadNames;
} skaldSocketCtx;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


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
 * an alarm. This function takes ownership of `msg` (it will actually
 * unreference it).
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


/** Take action on an incoming messge
 *
 * This function takes ownership of `msg`.
 *
 * @param sockid [in]     skal-net socket that received this message
 * @param ctx    [in,out] Context of socket that received this message
 * @param msg    [in]     Message to process; must not be NULL
 *
 * @return `true` if message successfully processed, `false` otherwise (in which
 *         case it must be unreferenced)
 */
static bool skaldProcessMsg(int sockid, skaldSocketCtx* ctx, SkalMsg* msg);



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


/** Repeat pipe client sockid for easy access */
static int gPipeClientSockid = -1;


/** Information about all managed and domain threads
 *
 * Map is made of `skaldThread`, and the key is the thread name.
 */
static CdsMap* gThreads = NULL;


/** Alarms currently active */
static CdsMap* gAlarms = NULL;


/** Domain this skald manages */
static char gDomain[SKAL_DOMAIN_NAME_MAX] = "local";


/** Flag to indicate that skald has terminated */
static bool gTerminated = false;



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
    SKALASSERT(params->localAddrPath != NULL);
    SKALASSERT(params->localAddrPath[0] != '\0');

    gNet = SkalNetCreate(params->pollTimeout_us, skaldCtxUnref);

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
    SkalNetAddr addr;
    addr.type = SKAL_NET_TYPE_PIPE;
    int sockid = SkalNetServerCreate(gNet, &addr, 0, ctx, 0);
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

    // TODO: Connect to other skald's
    (void)params->peers;
    (void)params->npeers;

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_SERVER;
    snprintf(ctx->name, sizeof(ctx->name), "local-server");
    addr.type = SKAL_NET_TYPE_UNIX_SEQPACKET;
    snprintf(addr.unix.path, sizeof(addr.unix.path),
            "%s", params->localAddrPath);
    (void)SkalNetServerCreate(gNet, &addr, 0, ctx, 0);

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
                CdsMapClear(ctx->threadNames);
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
                                "skal-invalid-json", SKAL_ALARM_ERROR,
                                true, false, "From process '%s'", ctx->name);
                        skaldAlarmProcess(alarm);
                    } else {
                        if (!skaldProcessMsg(event->sockid, ctx, msg)) {
                            SkalMsgUnref(msg);
                        }
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

    SkalNetDestroy(gNet);
    CdsMapDestroy(gThreads);
    CdsMapDestroy(gAlarms);

    gTerminated = true;
}


void SkaldTerminate(void)
{
    char c = 'x';
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gPipeClientSockid, &c, 1);
    SKALASSERT(SKAL_NET_SEND_OK == result);
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


static void skaldAlarmProcess(SkalAlarm* alarm)
{
    const char* origin = SkalAlarmOrigin(alarm);
    if (NULL == origin) {
        origin = "";
    }
    skaldAlarm* item;
    char key[sizeof(item->key)];
    int n = snprintf(key, sizeof(key), "%s#%s", origin, SkalAlarmType(alarm));
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


static const char* skaldSocketTypeToString(skaldSocketType type)
{
    const char* str = "UNKNOWN";
    switch (type) {
        case SKALD_SOCKET_PIPE_SERVER  : str = "PIPE_SERVER" ; break;
        case SKALD_SOCKET_PIPE_CLIENT  : str = "PIPE_CLIENT" ; break;
        case SKALD_SOCKET_DOMAIN_PEER  : str = "DOMAIN_PEER" ; break;
        case SKALD_SOCKET_FOREIGN_PEER : str = "FOREIGN_PEER"; break;
        case SKALD_SOCKET_SERVER       : str = "SERVER"      ; break;
        case SKALD_SOCKET_PROCESS      : str = "PROCESS"     ; break;
    }
    return str;
}


static void skaldWrongSocketType(int sockid, const skaldSocketCtx* ctx,
        const char* msgtype, const char* extra)
{
    SkalAlarm* alarm = SkalAlarmCreate(
            "skal-wrong-socket-type",
            SKAL_ALARM_ERROR,
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


static bool skaldProcessMsg(int sockid, skaldSocketCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(msg != NULL);

    // Basic checks on `msg`
    const char* sender = SkalMsgSender(msg);
    const char* recipient = SkalMsgRecipient(msg);
    if (NULL == skaldDomain(sender)) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-invalid-msg-sender-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the sender has no domain: '%s'",
                sender);
        skaldAlarmProcess(alarm);
        return false;
    }
    if (NULL == skaldDomain(recipient)) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-invalid-msg-recipient-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the recipient has no domain: '%s'",
                recipient);
        skaldAlarmProcess(alarm);
        return false;
    }

    // Take action depending on message type
    const char* type = SkalMsgType(msg);
    if (strcmp(type, "skal-master-born") == 0) {
        // The `skal-master` thread of a process provides us with information
        if (ctx->type != SKALD_SOCKET_PROCESS) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS");
            return false;
        }
        const char* name = SkalMsgGetString(msg, "name");
        snprintf(ctx->name, sizeof(ctx->name), "%s", name);

        // Send a `skal-domain` message in response
        SkalMsg* resp = SkalMsgCreate("skal-domain", "skal-master", 0, NULL);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(resp, "domain", gDomain);
        skaldMsgSendTo(resp, sockid);

        SkalMsgUnref(msg);

    } else if (strcmp(type, "skal-born") == 0) {
        // A managed or domain thread has been born
        if (    (ctx->type != SKALD_SOCKET_PROCESS)
             || (ctx->type != SKALD_SOCKET_DOMAIN_PEER)) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS or SKALD_SOCKET_DOMAIN_PEER");
            return false;
        }

        const char* domain = skaldDomain(sender);
        SKALASSERT(domain != NULL);
        if (strcmp(domain, gDomain) != 0) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-invalid-sender-domain",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-born message from '%s', which is on a different domain than mine (%s)",
                    sender, gDomain);
            skaldAlarmProcess(alarm);
            return false;
        }

        skaldThread* thread = SkalMallocZ(sizeof(*thread));
        thread->sockid = sockid;
        snprintf(thread->name, sizeof(thread->name), "%s", sender);
        bool inserted = CdsMapInsert(gThreads, (void*)sender, &thread->item);
        SKALASSERT(inserted);

        skaldNameMapItem* item = SkalMallocZ(sizeof(*item));
        item->ref = 1;
        snprintf(item->name, sizeof(item->name), "%s", sender);
        inserted = CdsMapInsert(ctx->threadNames,
                (void*)(item->name), &item->item);
        SKALASSERT(inserted);

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform all domain peers
        }
        SkalMsgUnref(msg);

    } else if (strcmp(type, "skal-died") == 0) {
        // A managed or domain thread just died
        if (    (ctx->type != SKALD_SOCKET_PROCESS)
             || (ctx->type != SKALD_SOCKET_DOMAIN_PEER)) {
            skaldWrongSocketType(sockid, ctx, type,
                    "expected SKALD_SOCKET_PROCESS or SKALD_SOCKET_DOMAIN_PEER");
            return false;
        }
        bool removed = CdsMapRemove(gThreads, (void*)sender);
        if (!removed) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received skal-died for unknown thread '%s'",
                    sender);
            skaldAlarmProcess(alarm);
        }
        removed = CdsMapRemove(ctx->threadNames, (void*)sender);
        if (!removed) {
            SkalAlarm* alarm = SkalAlarmCreate("skal-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received skal-died for unknown thread '%s' in process/peer '%s'",
                    sender, ctx->name);
            skaldAlarmProcess(alarm);
        }

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform domain peers
        }
        SkalMsgUnref(msg);

    } else if (strcmp(type, "skal-ntf-xon") == 0) {
        const char* domain = skaldDomain(recipient);
        SKALASSERT(domain != NULL);
        if (    (strcmp(domain, gDomain) == 0)
             && (CdsMapSearch(gThreads, (void*)recipient) == NULL)) {
            // `sender` is blocked by `recipient`, but `recipient` does not
            // exist anymore
            //  => Unblock `sender` and don't forward the original msg
            SkalMsg* resp = SkalMsgCreate("skal-xon", sender, 0, NULL);
            SkalMsgSetSender(resp, recipient);
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            skaldRouteMsg(resp);
            SkalMsgUnref(msg);
        } else {
            skaldRouteMsg(msg);
        }

    } else {
        skaldRouteMsg(msg);
    }

    return true;
}


static void skaldDropMsg(SkalMsg* msg)
{
    SKALASSERT(msg != NULL);

    SkalAlarm* alarm = SkalAlarmCreate("skal-drop-no-recipient",
            SKAL_ALARM_WARNING, true, false,
            "Received msg '%s' with unknwon recipient '%s'",
            SkalMsgType(msg), SkalMsgRecipient(msg));
    skaldAlarmProcess(alarm);

    if (SkalMsgFlags(msg) | SKAL_MSG_FLAG_NTF_DROP) {
        SkalMsg* resp = SkalMsgCreate("skal-drop-no-recipient",
                SkalMsgSender(msg), 0, NULL);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        skaldRouteMsg(resp);
    }

    SkalMsgUnref(msg);
}


static void skaldRouteMsg(SkalMsg* msg)
{
    const char* recipient = SkalMsgRecipient(msg);
    const char* domain = skaldDomain(recipient);
    if (NULL == domain) {
        SkalAlarm* alarm = SkalAlarmCreate("skal-invalid-msg-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Invalid message to route: recipient has no domain: '%s'",
                recipient);
        skaldAlarmProcess(alarm);
        SkalMsgUnref(msg);
        return;
    }
    if (strcmp(domain, gDomain) == 0) {
        skaldThread* thread = (skaldThread*)CdsMapSearch(gThreads,
                (void*)recipient);
        if (NULL == thread) {
            skaldDropMsg(msg);
        } else {
            skaldMsgSendTo(msg, thread->sockid);
        }
    } else {
        // The recipient is in another domain
        //  => Look up foreign skald to send to
        // TODO
        SKALPANIC_MSG("Not yet implemented");
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
            SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, sockid,
                    json, strlen(json) + 1);
            free(json);
            if (result != SKAL_NET_SEND_OK) {
                SkalAlarm* alarm = SkalAlarmCreate("skal-send-fail",
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
