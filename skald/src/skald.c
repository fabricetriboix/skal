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

#include "skald.h"
#include "skald-alarm.h"
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


/** Reason why a message is being dropped */
typedef enum {
    SKALD_DROP_TTL,         ///< Message TTL has reached 0
    SKALD_DROP_NO_RECIPIENT ///< Message recipient does not exist
} skaldDropReason;


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
    char*      name;
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
    char* name;
} skaldThread;


/** Structure that holds information about a group subscriber
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsListItem item;

    /** Name of the thread that subscribed to this group */
    char* thread;

    /** Pattern to filter on messages names
     *
     * If this starts with "regex:", the `regex` property below will not be NULL
     * and should be used instead. If it does not start with "regex:", only
     * messages whose names start with `pattern` are sent to `thread`.
     */
    char* pattern;

    /** Regular expression to filter message names
     *
     * This can be NULL, in which case this filter is not applied.
     */
    SkalPlfRegex* regex;
} skaldSubscriber;


/** Structure that holds information about a group
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsMapItem item;

    /** Group name */
    char* name;

    /** List of subscribers
     *
     * A subscriber is uniquely identified as a pair "thread name" + "pattern".
     * This allows a thread to subscribe multiple times with different patterns,
     * if it wishes so.
     *
     * This is a list of `skaldSubscriber`.
     */
    CdsList* subscribers;
} skaldGroup;


/** Structure that holds information related to a socket
 *
 * NB: We do not keep track of reference count, because such a structure is only
 * referenced by one and only one skal-net socket.
 */
typedef struct {
    /** What type of socket it is */
    skaldSocketType type;

    /** Name representative of that socket (for debug messages) */
    char* name;

    /** Domain of the peer on the other side of that socket
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_PEER` or
     * `SKALD_SOCKET_FOREIGN_PEER`.
     */
    char* domain;

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


/** De-reference a name item */
static void skaldNameMapItemUnref(CdsMapItem* mitem);


/** De-reference a group */
static void skaldGroupUnref(CdsMapItem* item);


/** De-reference a subscriber */
static void skaldSubscriberUnref(CdsListItem* item);


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
static void skaldThreadUnref(CdsMapItem* mitem);


/** Drop a message
 *
 * This function will inform the sender if it requested so. It will also raise
 * an alarm. This function takes ownership of `msg`.
 *
 * @param msg    [in,out] Message to drop; must not be NULL
 * @param reason [in]     Why is this message dropped
 */
static void skaldDropMsg(SkalMsg* msg, skaldDropReason reason);


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


/** Get the structure of a given group, create it if necessary
 *
 * @param group [in] The group name; must not be NULL
 *
 * @return The group structure; this function never returns NULL
 */
static skaldGroup* skaldGetGroup(const char* group);


/** Add a subscriber to the given group
 *
 * @param group   [in] Name of the group where to add the subscriber; must not
 *                     be NULL
 * @param thread  [in] Name of the thread that wants to subscribe; must be a
 *                     valid thread name
 * @param pattern [in] Pattern to match; may be NULL
 *
 * The `pattern` parameter may be one of the following:
 *  - NULL or the empty string: `thread` will be forwarded all messages sent to
 *    this group
 *  - A string that starts with "regex:": `thread` will be forwarded all
 *    messages whose name match the regular expression
 *  - Otherwise, `thread` will be forwarded all messages whose name start with
 *    the given string
 */
static void skaldGroupAddSubscriber(const char* group,
        const char* thread, const char* pattern);


/** Remove a subscriber from the given group
 *
 * @param group   [in] Name of the group from where to remove the subscriber;
 *                     must not be NULL
 * @param thread  [in] Name of the thread that wants to unsubscribe; must be a
 *                     valid thread name
 * @param pattern [in] Pattern to match; may be NULL
 */
static void skaldGroupRemoveSubscriber(const char* group,
        const char* thread, const char* pattern);


/** Send the given message to all subcribers of the given multicast group
 *
 * The ownership of `msg` stays with the caller.
 *
 * @param group [in] Name of the group; must not be NULL
 * @param msg   [in] Message to send; must not be NULL
 */
static void skaldMulticastDispatch(const char* group, const SkalMsg* msg);



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


/** Information about groups
 *
 * Map is made of `skaldGroup`, and the key is the group name.
 */
static CdsMap* gGroups = NULL;


/** Full name of this skald 'thread' */
static char* gName = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldRun(const SkaldParams* params)
{
    SKALASSERT(NULL == gSkaldThread);
    SKALASSERT(NULL == gNet);
    SKALASSERT(-1 == gPipeClientSockid);
    SKALASSERT(NULL == gThreads);
    SKALASSERT(NULL == gName);
    SKALASSERT(NULL == gGroups);

    SkaldAlarmInit();

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
    gName = SkalSPrintf("skald@%s", SkalDomain());

    gNet = SkalNetCreate(skaldCtxUnref);

    gGroups = CdsMapCreate("groups",          // name
                           0,                 // capacity
                           SkalStringCompare, // compare
                           NULL,              // cookie
                           NULL,              // keyUnref
                           skaldGroupUnref);  // itemUnref

    gThreads = CdsMapCreate("threads",         // name
                            0,                 // capacity
                            SkalStringCompare, // compare
                            NULL,              // cookie
                            NULL,              // keyUnref
                            skaldThreadUnref); // itemUnref

    // Create pipe to allow skald to terminate cleanly
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_SERVER;
    ctx->name = SkalStrdup("pipe-server");
    int sockid = SkalNetServerCreate(gNet, "pipe://", 0, ctx, 0);
    SKALASSERT(sockid >= 0);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(sockid == event->sockid);
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_CLIENT;
    ctx->name = SkalStrdup("pipe-client");
    bool contextSet = SkalNetSetContext(gNet, event->conn.commSockid, ctx);
    gPipeClientSockid = event->conn.commSockid;
    SKALASSERT(contextSet);
    SkalNetEventUnref(event);

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_SERVER;
    ctx->name = SkalStrdup("local-server");
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
    CdsMapDestroy(gGroups);

    free(gName);
    gName = NULL;

    SkaldAlarmExit();
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
                    SkalMsg* msg = SkalMsgCreateFromJson(json);
                    if (NULL == msg) {
                        SkaldAlarmNew("skal-protocol-invalid-json",
                                SKAL_ALARM_ERROR, true, false,
                                "From process '%s'", ctx->name);
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


static void skaldNameMapItemUnref(CdsMapItem* mitem)
{
    skaldNameMapItem* item = (skaldNameMapItem*)mitem;
    free(item->name);
    free(item);
}


static void skaldGroupUnref(CdsMapItem* mitem)
{
    skaldGroup* group = (skaldGroup*)mitem;
    SKALASSERT(group != NULL);
    SKALASSERT(group->subscribers != NULL);
    CdsListDestroy(group->subscribers);
    free(group->name);
    free(group);
}


static void skaldSubscriberUnref(CdsListItem* item)
{
    skaldSubscriber* subscriber = (skaldSubscriber*)item;
    SKALASSERT(subscriber != NULL);
    free(subscriber->thread);
    free(subscriber->pattern);
    SkalPlfRegexDestroy(subscriber->regex);
    free(subscriber);
}


static void skaldCtxUnref(void* context)
{
    SKALASSERT(context != NULL);
    skaldSocketCtx* ctx = (skaldSocketCtx*)context;
    if (ctx->threadNames != NULL) {
        CdsMapDestroy(ctx->threadNames);
    }
    free(ctx->name);
    free(ctx->domain);
    free(ctx);
}


static void skaldHandleProcessConnection(int commSockid)
{
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PROCESS;
    ctx->name = SkalSPrintf("process (%d)", commSockid);
    ctx->threadNames = CdsMapCreate(NULL,                   // name
                                    0,                      // capacity
                                    SkalStringCompare,      // compare
                                    NULL,                   // cookie
                                    NULL,                   // keyUnref
                                    skaldNameMapItemUnref); // itemUnref
    bool contextSet = SkalNetSetContext(gNet, commSockid, ctx);
    SKALASSERT(contextSet);
}


static void skaldThreadUnref(CdsMapItem* mitem)
{
    skaldThread* thread = (skaldThread*)mitem;
    SKALASSERT(thread != NULL);
    free(thread->name);
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
        SkaldAlarmNew("skal-protocol-sender-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the sender has no domain: '%s' (message name: '%s')",
                sender,
                msgName);
        SkalMsgUnref(msg);
        return;
    }
    const char* recipient = SkalMsgRecipient(msg);
    if (NULL == skaldDomain(recipient)) {
        SkaldAlarmNew("skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the recipient has no domain: '%s' (message name: '%s')",
                recipient,
                msgName);
        SkalMsgUnref(msg);
        return;
    }

    if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_MULTICAST) {
        skaldMulticastDispatch(recipient, msg);
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
            SkalMsg* resp = SkalMsgCreate("skal-xon", sender);
            SkalMsgSetSender(resp, recipient);
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            skaldRouteMsg(resp);
            SkalMsgUnref(msg);
            return;
        }
        // else: The recipient is on a different domain, just forward the
        // message to the appropriate skald.
    }

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
        if (!SkalMsgHasAsciiString(msg, "name")) {
            SkaldAlarmNew("skal-protocol-missing-field",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-init-master-born' message from '%s' without a 'name' field",
                    sender);

        } else {
            const char* name = SkalMsgGetString(msg, "name");
            // `ctx->name` should be NULL here, but let's just be safe
            free(ctx->name);
            ctx->name = SkalStrdup(name);

            // Respond with the domain name
            SkalMsg* resp = SkalMsgCreate("skal-init-domain", "skal-master");
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(resp, "domain", SkalDomain());
            skaldMsgSendTo(resp, sockid);
        }

    } else if (strcmp(msgName, "skal-born") == 0) {
        // A managed or domain thread has been born
        const char* domain = skaldDomain(sender);
        SKALASSERT(domain != NULL);
        if (strcmp(domain, SkalDomain()) != 0) {
            SkaldAlarmNew("skal-protocol-wrong-sender-domain",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', which is on a different domain than mine (%s)",
                    sender, SkalDomain());

        } else if (CdsMapSearch(gThreads, (void*)sender) != NULL) {
            SkaldAlarmNew("skal-conflict-duplicate-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', but a thread with that name is already registered",
                    sender);

        } else if (CdsMapSearch(ctx->threadNames, (void*)sender) != NULL) {
            SkaldAlarmNew("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' not registered globally, but is listed for process '%s'; this is impossible",
                    sender, ctx->name);

        } else {
            skaldThread* thread = SkalMallocZ(sizeof(*thread));
            thread->sockid = sockid;
            thread->name = SkalStrdup(sender);
            bool inserted = CdsMapInsert(gThreads,
                    (void*)thread->name, &thread->item);
            SKALASSERT(inserted);

            skaldNameMapItem* item = SkalMallocZ(sizeof(*item));
            item->name = SkalStrdup(sender);
            inserted = CdsMapInsert(ctx->threadNames, item->name, &item->item);
            SKALASSERT(inserted);

            if (SKALD_SOCKET_PROCESS == ctx->type) {
                // TODO: inform all domain peers
            }
        }

    } else if (strcmp(msgName, "skal-died") == 0) {
        // A managed or domain thread just died
        bool removed = CdsMapRemove(gThreads, (void*)sender);
        if (!removed) {
            SkaldAlarmNew("skal-conflict-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received 'skal-died' for unknown thread '%s'",
                    sender);
        }
        removed = CdsMapRemove(ctx->threadNames, (void*)sender);
        if (!removed) {
            SkaldAlarmNew("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' is registered globally, but not for process '%s'; this is impossible",
                    sender, ctx->name);
        }

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform domain peers
        }

    } else if (strcmp(msgName, "skal-ping") == 0) {
        SkalMsg* resp = SkalMsgCreate("skal-pong", sender);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        skaldRouteMsg(resp);

    } else if (strcmp(msgName, "skal-subscribe") == 0) {
        // The sender wants to subscribe to a group
        if (!SkalMsgHasAsciiString(msg, "group")) {
            SkaldAlarmNew("skal-protocol-subscribe-without-group",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-subscribe message from '%s' without a 'group' field",
                    sender);
        } else {
            const char* group = SkalMsgGetString(msg, "group");
            const char* pattern = NULL;
            if (SkalMsgHasAsciiString(msg, "pattern")) {
                pattern = SkalMsgGetString(msg, "pattern");
            }
            if ((pattern != NULL) && ('\0' == pattern[0])) {
                pattern = NULL;
            }
            skaldGroupAddSubscriber(group, sender, pattern);
        }

    } else if (strcmp(msgName, "skal-unsubscribe") == 0) {
        // The sender wants to unsubscribe from a group
        if (!SkalMsgHasAsciiString(msg, "group")) {
            SkaldAlarmNew("skal-protocol-unsubscribe-without-group",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-unsubscribe message from '%s' without a 'group' field",
                    sender);
        } else {
            const char* group = SkalMsgGetString(msg, "group");
            const char* pattern = NULL;
            if (SkalMsgHasAsciiString(msg, "pattern")) {
                pattern = SkalMsgGetString(msg, "pattern");
            }
            if ((pattern != NULL) && ('\0' == pattern[0])) {
                pattern = NULL;
            }
            skaldGroupRemoveSubscriber(group, sender, pattern);
        }

    } else {
        SkaldAlarmNew("skal-protocol-unknown-message",
                SKAL_ALARM_NOTICE, true, false,
                "Received unknown message '%s' from '%s'",
                msgName, sender);
    }

    SkalMsgUnref(msg);
}


static void skaldDropMsg(SkalMsg* msg, skaldDropReason reason)
{
    SKALASSERT(msg != NULL);

    SkalAlarm* alarm = NULL;
    const char* reasonStr = NULL;
    char* extraStr = NULL;
    switch (reason) {
    case SKALD_DROP_TTL :
        alarm = SkalAlarmCreate("skal-drop-ttl",
                SKAL_ALARM_WARNING, true, false,
                "Message '%s' TTL has reached 0; message dropped",
                SkalMsgName(msg));
        reasonStr = "ttl-expired";
        break;
    case SKALD_DROP_NO_RECIPIENT :
        alarm = SkalAlarmCreate("skal-drop",
                SKAL_ALARM_WARNING, true, false,
                "Can't route message '%s' because I know nothing about its recipient '%s'; message dropped",
                SkalMsgName(msg), SkalMsgRecipient(msg));
        reasonStr = "no-recipient";
        extraStr = SkalSPrintf("Thread '%s' does not exist",
                SkalMsgRecipient(msg));
        break;
    default :
        SKALPANIC_MSG("Unknown drop reason: %d", (int)reason);
    }
    SkaldAlarmProcess(alarm);

    if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP) {
        SkalMsg* resp = SkalMsgCreate("skal-error-drop", SkalMsgSender(msg));
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        if (reasonStr != NULL) {
            SkalMsgAddString(resp, "reason", reasonStr);
        }
        if (extraStr != NULL) {
            SkalMsgAddString(resp, "extra", extraStr);
        }
        skaldRouteMsg(resp);
    }

    free(extraStr);
    SkalMsgUnref(msg);
}


static void skaldRouteMsg(SkalMsg* msg)
{
    const char* recipient = SkalMsgRecipient(msg);
    const char* domain = skaldDomain(recipient);

    SkalMsgDecrementTtl(msg);
    if (SkalMsgTtl(msg) <= 0) {
        skaldDropMsg(msg, SKALD_DROP_TTL);

    } else if (NULL == domain) {
        SkaldAlarmNew("skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Can't route message '%s': recipient has no domain: '%s'",
                SkalMsgName(msg), recipient);
        SkalMsgUnref(msg);

    } else if (strcmp(domain, SkalDomain()) == 0) {
        skaldThread* thread = (skaldThread*)CdsMapSearch(gThreads,
                (void*)recipient);
        if (thread != NULL) {
            skaldMsgSendTo(msg, thread->sockid);
        } else if (strcmp(recipient, gName) == 0) {
            SkaldAlarmNew("skal-conflict-circular-msg",
                    SKAL_ALARM_WARNING, true, false,
                    "Can't route message '%s': recipient '%s' is myself",
                    SkalMsgName(msg), recipient);
        } else {
            skaldDropMsg(msg, SKALD_DROP_NO_RECIPIENT);
        }

    } else {
        // The recipient is in another domain
        //  => Look up foreign skald to send to
        // TODO
        char* json = SkalMsgToJson(msg);
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
            SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, sockid,
                    json, strlen(json) + 1);
            free(json);
            if (result != SKAL_NET_SEND_OK) {
                SkaldAlarmNew("skal-io-send-fail",
                        SKAL_ALARM_ERROR, true, false,
                        "Failed to send over socket '%s'", ctx->name);
                SkalNetSocketDestroy(gNet, sockid);
            }
        }
        break;
    default :
        SKALPANIC_MSG("Can't send a msg over socket type %d", (int)ctx->type);
    }
    SkalMsgUnref(msg);
}


static skaldGroup* skaldGetGroup(const char* group)
{
    SKALASSERT(SkalIsAsciiString(group));
    SKALASSERT(gGroups != NULL);
    skaldGroup* g = (skaldGroup*)CdsMapSearch(gGroups, (void*)group);
    if (NULL == g) {
        g = SkalMallocZ(sizeof(*g));
        g->name = SkalStrdup(group);
        g->subscribers = CdsListCreate(NULL, 0, skaldSubscriberUnref);
    }
    return g;
}


static void skaldGroupAddSubscriber(const char* group,
        const char* thread, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(group));
    SKALASSERT(SkalIsAsciiString(thread));
    skaldGroup* g = skaldGetGroup(group);

    // First, ensure that this subscriber does not exist already
    CDSLIST_FOREACH(g->subscribers, skaldSubscriber, subscriber) {
        if (    (SkalStrcmp(subscriber->thread, thread) == 0)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            return; // Subscriber already exists, nothing to do
        }
    }

    skaldSubscriber* subscriber = SkalMallocZ(sizeof(*subscriber));
    subscriber->thread = SkalStrdup(thread);
    subscriber->pattern = SkalStrdup(pattern);
    if (SkalStartsWith(subscriber->pattern, "regex:")) {
        subscriber->regex = SkalPlfRegexCreate(subscriber->pattern + 6);
        if (NULL == subscriber->regex) {
            SkalLog("SKALD: Received a skal-subscribe message from '%s' with an invalid regex '%s'; request ignored",
                    thread, subscriber->pattern + 6);
            skaldSubscriberUnref(&subscriber->item);
            return;
        }
    }
    bool inserted = CdsListPushBack(g->subscribers, &subscriber->item);
    SKALASSERT(inserted);
}


void skaldGroupRemoveSubscriber(const char* group,
        const char* thread, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(group));
    SKALASSERT(SkalIsAsciiString(thread));
    skaldGroup* g = skaldGetGroup(group);

    CDSLIST_FOREACH(g->subscribers, skaldSubscriber, subscriber) {
        if (    (SkalStrcmp(subscriber->thread, thread) == 0)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            CdsListRemove(&subscriber->item);
            skaldSubscriberUnref(&subscriber->item);
            break;
        }
    }
}


static void skaldMulticastDispatch(const char* group, const SkalMsg* msg)
{
    SKALASSERT(SkalIsAsciiString(group));
    SKALASSERT(msg != NULL);

    const char* msgName = SkalMsgName(msg);
    skaldGroup* g = skaldGetGroup(group);
    CDSLIST_FOREACH(g->subscribers, skaldSubscriber, subscriber) {
        bool forward = true;
        if (subscriber->regex != NULL) {
            if (!SkalPlfRegexRun(subscriber->regex, msgName)) {
                forward = false;
            }
        } else if (subscriber->pattern != NULL) {
            if (!SkalStartsWith(msgName, subscriber->pattern)) {
                forward = false;
            }
        }

        if (forward) {
            SkalMsg* copy = SkalMsgCopy(msg, subscriber->thread);
            skaldRouteMsg(copy);
        }
    } // for each subscriber
}
