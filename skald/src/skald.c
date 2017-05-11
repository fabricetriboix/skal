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

    /** Local server - for processes to connect to me */
    SKALD_SOCKET_SERVER,

    /** Someone just connected to us, but we don't know who yet */
    SKALD_SOCKET_UNDETERMINED,

    /** Local comm - one per process */
    SKALD_SOCKET_PROCESS,

    /** Other skald in the same domain */
    SKALD_SOCKET_DOMAIN_SKALD,

    /** Skald in other domains */
    SKALD_SOCKET_FOREIGN_SKALD
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
    char* threadName;
} skaldThread;


/** Structure that holds information about a group subscriber which is a thread
 *
 * A subscription is uniquely identified by the pair "thread name + pattern".
 * This allows the same thread to subscribe mulitple times with different
 * patterns.
 *
 * Please note a domain thread can't be a subscriber. A domain thread must
 * subscribe to its own skald.
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsListItem item;

    /** Name of the thread that subscribed to this group */
    char* threadName;

    /** Pattern to filter on messages names
     *
     * If this starts with "regex:", the `regex` property below will not be NULL
     * and should be used instead. If it does not start with "regex:", only
     * messages whose names start with `pattern` are sent to `thread`.
     *
     * NB: In the case of a regex pattern, it may seem redundant to keep the
     * orignal regex string. We need to keep it in order to be able to find back
     * the pair "thread/pattern" which uniquely identifies a subscription.
     */
    char* pattern;

    /** Regular expression to filter message names
     *
     * This can be NULL, in which case this filter is not applied.
     */
    SkalPlfRegex* regex;
} skaldThreadSubscriber;


/** Structure that holds information about a group subscriber which is a skald
 *
 * The skald could be in the same domain or foreign.
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsListItem item;

    /** Socket identifier for the skald subscriber */
    int sockid;

    /** Pattern to filter on messages names
     *
     * If this starts with "regex:", the `regex` property below will not be NULL
     * and should be used instead. If it does not start with "regex:", only
     * messages whose names start with `pattern` are sent to `thread`.
     *
     * NB: In the case of a regex pattern, it may seem redundant to keep the
     * orignal regex string. We need to keep it in order to be able to find back
     * the pair "sockid/pattern" which uniquely identifies a subscription.
     */
    char* pattern;

    /** Regular expression to filter message names
     *
     * This can be NULL, in which case this filter is not applied.
     */
    SkalPlfRegex* regex;
} skaldSkaldSubscriber;


/** Structure that holds information about a group
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsMapItem item;

    /** Group name */
    char* groupName;

    /** List of thread subscribers
     *
     * A thread subscriber is uniquely identified as a pair "thread name +
     * pattern". This allows a thread to subscribe multiple times with different
     * patterns, if it wishes so.
     *
     * This is a list of `skaldThreadSubscriber`.
     *
     * NB: I chose a list here as it's mostly (and often) read sequentially to
     * forward messages. Only from time to time it is accessed to add/remove
     * subscribers; walking through the list is acceptable then, as it is done
     * many times over when forwarding multicast messages.
     */
    CdsList* threadSubscribers;

    /** List of skald subscribers
     *
     * This is a list of `skaldSkaldSubscriber`.
     *
     * NB: I chose a list here as it's mostly (and often) read sequentially to
     * forward messages. Only from time to time it is accessed to add/remove
     * subscribers; walking through the list is acceptable then, as it is done
     * many times over when forwarding multicast messages.
     */
    CdsList* skaldSubscribers;
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

    /** Domain of the skald on the other side of that socket
     *
     * Meaningful only if `type` is `SKALD_SOCKET_FOREIGN_SKALD`; stays NULL
     * otherwise.
     */
    char* domain;

    /** Map of thread names - made of `skaldNameMapItem`
     *
     * These are the names of the threads that live on the other side of this
     * socket. They may be in a process or in a skald in the same domain.
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_SKALD` or
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


/** De-reference a thread subscriber */
static void skaldThreadSubscriberUnref(CdsListItem* item);


/** De-reference a skald subscriber */
static void skaldSkaldSubscriberUnref(CdsListItem* item);


/** Function to de-reference a skal-net socket context */
static void skaldCtxUnref(void* context);


/** Function to de-reference a `skaldThread` */
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


/** Route a message going out of skald
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


/** Handle a message received from someone who just connected
 *
 * @param ctx   [in] Socket context; must not be NULL; must be of type
 *                   SKALD_SOCKET_UNDETERMINED
 * @param event [in] Event received; must not be NULL; must be of type
 *                   SKAL_NET_EV_IN
 *
 * @return `true` if OK, `false` if invalid message
 */
static bool skaldHandleDataInFromUndetermined(skaldSocketCtx* ctx,
        SkalNetEvent* event);


/** Take action on an incoming message from a process
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


/** Take action on an incoming message for this skald from a process
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


/** Get the object representing the given group, creating it if necessary
 *
 * @param groupName [in] The group name; must not be NULL
 *
 * @return The group structure; this function never returns NULL
 */
static skaldGroup* skaldGetGroup(const char* groupName);


/** Add a thread subscriber to the given group
 *
 * @param groupName  [in] Name of the group where to add the thread subscriber;
 *                        must not be NULL
 * @param threadName [in] Name of the thread that wants to subscribe; must be a
 *                        valid thread name
 * @param pattern    [in] Pattern to match; may be NULL
 *
 * The `pattern` parameter may be one of the following:
 *  - NULL or the empty string: the thread will be forwarded all messages sent
 *    to this group
 *  - A string that starts with "regex:": the thread will be forwarded all
 *    messages whose names match the regular expression
 *  - Otherwise, the thread will be forwarded all messages whose names start
 *    with the given string
 */
static void skaldGroupSubscribeThread(const char* groupName,
        const char* threadName, const char* pattern);


/** Remove a thread subscriber from the given group
 *
 * A subscription is uniquely idenfitied by the pair "thread name + pattern".
 * Unsubscribing for a given pattern will not unsubscribe the thread for other
 * patterns (if any).
 *
 * @param groupName  [in] Name of the group from where to remove the
 *                        thread subscriber; must not be NULL
 * @param threadName [in] Name of the thread that wants to unsubscribe; must be
 *                        a valid thread name
 * @param pattern    [in] Matching pattern; may be NULL
 */
static void skaldGroupUnsubscribeThread(const char* groupName,
        const char* threadName, const char* pattern);


#if 0
/** Add a skald subscriber to the given group
 *
 * @param groupName [in] Name of the group where to add the skald subscriber;
 *                       must not be NULL
 * @param sockid    [in] Socket identifier for this skald
 * @param pattern   [in] Pattern to match; may be NULL
 *
 * The `pattern` parameter may be one of the following:
 *  - NULL or the empty string: the thread will be forwarded all messages sent
 *    to this group
 *  - A string that starts with "regex:": the thread will be forwarded all
 *    messages whose names match the regular expression
 *  - Otherwise, the thread will be forwarded all messages whose names start
 *    with the given string
 */
static void skaldGroupSubscribeSkald(const char* groupName,
        int sockid, const char* pattern);


/** Remove a skald subscriber from the given group
 *
 * @param groupName [in] Name of the group from where to remove the skald
 *                       subscriber; must not be NULL
 * @param sockid    [in] Socket identifier for the skald to unsubscribe
 * @param pattern   [in] Matching pattern; may be NULL
 */
static void skaldGroupUnsubscribeSkald(const char* groupName,
        int sockid, const char* pattern);
#endif


/** Unsubscribe all threads of a process from all groups
 *
 * @param ctx [in] Socket context; must not be NULL; must be of type
 *                 SKALD_SOCKET_PROCESS
 */
static void skaldGroupUnsubscribeAllThreads(skaldSocketCtx* ctx);


/** Send the given message to all subscribers of the given multicast group
 *
 * The ownership of `msg` stays with the caller. For thread subscribers, the
 * message will be forwarded only if its name matches the subscriber pattern.
 *
 * @param groupName [in] Name of the group; must not be NULL
 * @param msg       [in] Message to send; must not be NULL
 */
static void skaldMulticastDispatch(const char* groupName, const SkalMsg* msg);



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


/** The full name of this skald 'thread' */
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
    SKALASSERT(NULL == gGroups);
    SKALASSERT(NULL == gName);

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
    if (sockid < 0) {
        SkalLog("SKALD: Failed to create server socket '%s'", localUrl);
        exit(1);
    }

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

    gNet = NULL;
    gPipeClientSockid = -1;
    gThreads = NULL;
    gGroups = NULL;
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
                stop = true; // Someone called `SkaldTerminate()`
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

        case SKALD_SOCKET_SERVER :
            switch (event->type) {
            case SKAL_NET_EV_CONN :
                // Someone is connecting to us, we don't know who yet
                {
                    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
                    ctx->type = SKALD_SOCKET_UNDETERMINED;
                    int commSockid = event->conn.commSockid;
                    ctx->name = SkalSPrintf("undetermined (%d)", commSockid);
                    bool contextSet = SkalNetSetContext(gNet, commSockid, ctx);
                    SKALASSERT(contextSet);
                }
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on local server socket",
                        (int)event->type);
                break;
            }
            break;

        case SKALD_SOCKET_UNDETERMINED :
            switch (event->type) {
            case SKAL_NET_EV_ERROR :
                SkalLog("SKALD: Error reported on socket '%s'", ctx->name);
                // fallthrough
            case SKAL_NET_EV_DISCONN :
                SkalNetSocketDestroy(gNet, event->sockid);
                break;
            case SKAL_NET_EV_IN :
                {
                    char* json = (char*)(event->in.data);
                    // The string normally arrives null-terminated, but it's
                    // safer to enforce null termination
                    json[event->in.size_B - 1] = '\0';
                    bool ok = skaldHandleDataInFromUndetermined(ctx, event);
                    if (!ok) {
                        SkalNetSocketDestroy(gNet, event->sockid);
                    }
                }
                break;
            default :
                SKALPANIC_MSG("Unexpected event %d on local comm socket",
                        (int)event->type);
                break;
            }
            break;

        case SKALD_SOCKET_PROCESS :
            switch (event->type) {
            case SKAL_NET_EV_ERROR :
                SkaldAlarmNew("skal-io-socket-error",
                        SKAL_ALARM_ERROR, true, false,
                        "Error reported on socket '%s'", ctx->name);
                // fallthrough
            case SKAL_NET_EV_DISCONN :
                // This process is disconnecting from us
                // TODO: Notify other domain skald
                skaldGroupUnsubscribeAllThreads(ctx);
                // Ensure we don't talk to the dead while destroying the context
                CdsMapClear(ctx->threadNames);
                SkalNetSocketDestroy(gNet, event->sockid);
                break;
            case SKAL_NET_EV_IN :
                {
                    SKALASSERT(event->in.data != NULL);
                    SKALASSERT(event->in.size_B > 0);
                    char* json = (char*)(event->in.data);
                    // The string normally arrives null-terminated, but it's
                    // safer to enforce null termination
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

        case SKALD_SOCKET_DOMAIN_SKALD :
        case SKALD_SOCKET_FOREIGN_SKALD :
            SKALPANIC_MSG("Not yet implemented");
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
    SKALASSERT(group->threadSubscribers != NULL);
    CdsListDestroy(group->threadSubscribers);
    SKALASSERT(group->skaldSubscribers != NULL);
    CdsListDestroy(group->skaldSubscribers);
    free(group->groupName);
    free(group);
}


static void skaldThreadSubscriberUnref(CdsListItem* item)
{
    SKALASSERT(item != NULL);
    skaldThreadSubscriber* subscriber = (skaldThreadSubscriber*)item;
    free(subscriber->threadName);
    free(subscriber->pattern);
    SkalPlfRegexDestroy(subscriber->regex);
    free(subscriber);
}


static void skaldSkaldSubscriberUnref(CdsListItem* item)
{
    SKALASSERT(item != NULL);
    skaldSkaldSubscriber* subscriber = (skaldSkaldSubscriber*)item;
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


static void skaldThreadUnref(CdsMapItem* mitem)
{
    skaldThread* thread = (skaldThread*)mitem;
    SKALASSERT(thread != NULL);
    free(thread->threadName);
    free(thread);
}


static bool skaldHandleDataInFromUndetermined(skaldSocketCtx* ctx,
        SkalNetEvent* event)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_UNDETERMINED == ctx->type);
    SKALASSERT(NULL == ctx->threadNames);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_IN == event->type);
    SKALASSERT(event->in.data != NULL);
    SKALASSERT(event->in.size_B > 0);

    char* json = (char*)(event->in.data);
    // The string normally arrives null-terminated, but it's safer to enforce
    // null termination
    json[event->in.size_B - 1] = '\0';
    SkalMsg* msg = SkalMsgCreateFromJson(json);
    if (NULL == msg) {
        SkaldAlarmNew("skal-protocol-invalid-json", SKAL_ALARM_ERROR,
                true, false, "From socket '%s'", ctx->name);
        return false;
    }

    bool ok = true;
    const char* msgName = SkalMsgName(msg);
    if (SkalStartsWith(msgName, "skal-init-")) {
        // The peer is a process
        ctx->type = SKALD_SOCKET_PROCESS;
        free(ctx->name);
        ctx->name = SkalSPrintf("process (%d)", event->conn.commSockid);
        ctx->threadNames = CdsMapCreate(NULL,                   // name
                                        0,                      // capacity
                                        SkalStringCompare,      // compare
                                        NULL,                   // cookie
                                        NULL,                   // keyUnref
                                        skaldNameMapItemUnref); // itemUnref
        skaldHandleMsgFromProcess(event->sockid, ctx, msg);

    } else if (SkalStartsWith(msgName, "skald-init-")) {
        // The peer is a skald
        // TODO
        SkalMsgUnref(msg);

    } else {
        SkaldAlarmNew("skal-protocol-invalid-msg", SKAL_ALARM_ERROR,
                true, false,
                "From socket '%s'; expected 'skal-init-' or 'skald-init-'",
                ctx->name);
        ok = false;
        SkalMsgUnref(msg);
    }
    return ok;
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
    const char* senderName = SkalMsgSender(msg);
    if (NULL == skaldDomain(senderName)) {
        SkaldAlarmNew("skal-protocol-sender-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the sender has no domain: '%s' (message name: '%s')",
                senderName,
                msgName);
        SkalMsgUnref(msg);
        return;
    }
    const char* recipientName = SkalMsgRecipient(msg);
    if (NULL == skaldDomain(recipientName)) {
        SkaldAlarmNew("skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Received a message where the recipient has no domain: '%s' (message name: '%s')",
                recipientName,
                msgName);
        SkalMsgUnref(msg);
        return;
    }

    if (SkalMsgFlags(msg) & SKAL_MSG_FLAG_MULTICAST) {
        skaldMulticastDispatch(recipientName, msg);
        SkalMsgUnref(msg);
        return;
    }

    if (    (strcmp(recipientName, gName) == 0)
         || (SkalStartsWith(msgName, "skal-init-"))) {
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
        const char* domain = skaldDomain(recipientName);
        SKALASSERT(domain != NULL);
        if (    (strcmp(domain, SkalDomain()) == 0)
             && (CdsMapSearch(gThreads, (void*)recipientName) == NULL)) {
            // Unblock `sender` and don't forward the original msg
            SkalMsg* resp = SkalMsgCreate("skal-xon", senderName);
            SkalMsgSetSender(resp, recipientName);
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
    const char* senderName = SkalMsgSender(msg);
    if (strcmp(msgName, "skal-init-master-born") == 0) {
        // The `skal-master` thread of a process is uttering its first words!
        if (!SkalMsgHasAsciiString(msg, "name")) {
            SkaldAlarmNew("skal-protocol-missing-field",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-init-master-born' message from '%s' without a 'name' field",
                    senderName);

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
        const char* domain = skaldDomain(senderName);
        SKALASSERT(domain != NULL);
        if (strcmp(domain, SkalDomain()) != 0) {
            SkaldAlarmNew("skal-protocol-wrong-sender-domain",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', which is on a different domain than mine (%s)",
                    senderName, SkalDomain());

        } else if (CdsMapSearch(gThreads, (void*)senderName) != NULL) {
            SkaldAlarmNew("skal-conflict-duplicate-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', but a thread with that name is already registered",
                    senderName);

        } else if (CdsMapSearch(ctx->threadNames, (void*)senderName) != NULL) {
            SkaldAlarmNew("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' not registered globally, but is listed for process '%s'; this is impossible",
                    senderName, ctx->name);

        } else {
            skaldThread* thread = SkalMallocZ(sizeof(*thread));
            thread->sockid = sockid;
            thread->threadName = SkalStrdup(senderName);
            bool inserted = CdsMapInsert(gThreads,
                    thread->threadName, (CdsMapItem*)thread);
            SKALASSERT(inserted);

            skaldNameMapItem* item = SkalMallocZ(sizeof(*item));
            item->name = SkalStrdup(senderName);
            inserted = CdsMapInsert(ctx->threadNames,
                    item->name, (CdsMapItem*)item);
            SKALASSERT(inserted);

            if (SKALD_SOCKET_PROCESS == ctx->type) {
                // TODO: inform all domain peers
            }
        }

    } else if (strcmp(msgName, "skal-died") == 0) {
        // A managed or domain thread just died
        bool removed = CdsMapRemove(gThreads, (void*)senderName);
        if (!removed) {
            SkaldAlarmNew("skal-conflict-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received 'skal-died' for unknown thread '%s'",
                    senderName);
        }
        removed = CdsMapRemove(ctx->threadNames, (void*)senderName);
        if (!removed) {
            SkaldAlarmNew("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' is registered globally, but not for process '%s'; this is impossible",
                    senderName, ctx->name);
        }

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform domain peers
        }

    } else if (strcmp(msgName, "skal-ping") == 0) {
        SkalMsg* resp = SkalMsgCreate("skal-pong", senderName);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        skaldRouteMsg(resp);

    } else if (strcmp(msgName, "skal-subscribe") == 0) {
        // The sender wants to subscribe to a group
        if (!SkalMsgHasAsciiString(msg, "group")) {
            SkaldAlarmNew("skal-protocol-subscribe-without-group",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-subscribe message from '%s' without a 'group' field",
                    senderName);
        } else {
            const char* groupName = SkalMsgGetString(msg, "group");
            const char* domain = skaldDomain(groupName);
            if ((domain != NULL) && (SkalStrcmp(domain, SkalDomain()) != 0)) {
                SkaldAlarmNew("skal-protocol-subscribe-wrong-group",
                        SKAL_ALARM_WARNING, true, false,
                        "Received a skal-subscribe message from '%s' for group '%s' which is not in my domain (%s); request ignored",
                        senderName, groupName, SkalDomain());
            } else {
                char* tmp;
                if (NULL == domain) {
                    tmp = SkalSPrintf("%s@%s", groupName, SkalDomain());
                } else {
                    tmp = SkalStrdup(groupName);
                }
                const char* pattern = NULL;
                if (SkalMsgHasAsciiString(msg, "pattern")) {
                    pattern = SkalMsgGetString(msg, "pattern");
                }
                if ((pattern != NULL) && ('\0' == pattern[0])) {
                    pattern = NULL;
                }
                skaldGroupSubscribeThread(tmp, senderName, pattern);
                free(tmp);
            }
        }

    } else if (strcmp(msgName, "skal-unsubscribe") == 0) {
        // The sender wants to unsubscribe from a group
        if (!SkalMsgHasAsciiString(msg, "group")) {
            SkaldAlarmNew("skal-protocol-unsubscribe-without-group",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-unsubscribe message from '%s' without a 'group' field",
                    senderName);
        } else {
            const char* groupName = SkalMsgGetString(msg, "group");
            const char* pattern = NULL;
            if (SkalMsgHasAsciiString(msg, "pattern")) {
                pattern = SkalMsgGetString(msg, "pattern");
            }
            if ((pattern != NULL) && ('\0' == pattern[0])) {
                pattern = NULL;
            }
            skaldGroupUnsubscribeThread(groupName, senderName, pattern);
        }

    } else {
        SkaldAlarmNew("skal-protocol-unknown-message",
                SKAL_ALARM_NOTICE, true, false,
                "Received unknown message '%s' from '%s'",
                msgName, senderName);
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

    if (     (SkalMsgFlags(msg) & SKAL_MSG_FLAG_NTF_DROP)
         && !(SkalMsgFlags(msg) & SKAL_MSG_FLAG_MULTICAST)) {
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
    const char* recipientName = SkalMsgRecipient(msg);
    const char* domain = skaldDomain(recipientName);

    SkalMsgDecrementTtl(msg);
    if (SkalMsgTtl(msg) <= 0) {
        skaldDropMsg(msg, SKALD_DROP_TTL);

    } else if (NULL == domain) {
        SkaldAlarmNew("skal-protocol-recipient-has-no-domain",
                SKAL_ALARM_WARNING, true, false,
                "Can't route message '%s': recipient has no domain: '%s'",
                SkalMsgName(msg), recipientName);
        SkalMsgUnref(msg);

    } else if (strcmp(domain, SkalDomain()) == 0) {
        skaldThread* thread = (skaldThread*)CdsMapSearch(gThreads,
                (void*)recipientName);
        if (thread != NULL) {
            skaldMsgSendTo(msg, thread->sockid);
        } else if (strcmp(recipientName, gName) == 0) {
            SkaldAlarmNew("skal-conflict-circular-msg",
                    SKAL_ALARM_WARNING, true, false,
                    "Can't route message '%s': recipient '%s' is myself",
                    SkalMsgName(msg), recipientName);
        } else {
            skaldDropMsg(msg, SKALD_DROP_NO_RECIPIENT);
        }

    } else {
        // The recipient is in another domain
        //  => Look up foreign skald to send to
        // TODO
        SKALPANIC_MSG("Not yet implemented: message domain %s", domain);
    }
}


static void skaldMsgSendTo(SkalMsg* msg, int sockid)
{
    SKALASSERT(msg != NULL);
    skaldSocketCtx* ctx = (skaldSocketCtx*)SkalNetGetContext(gNet, sockid);
    SKALASSERT(ctx != NULL);

    switch (ctx->type) {
    case SKALD_SOCKET_DOMAIN_SKALD :
    case SKALD_SOCKET_FOREIGN_SKALD :
        SKALPANIC_MSG("SKALD comms not yet implemented");
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


static skaldGroup* skaldGetGroup(const char* groupName)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(gGroups != NULL);
    skaldGroup* group = (skaldGroup*)CdsMapSearch(gGroups, (void*)groupName);
    if (NULL == group) {
        group = SkalMallocZ(sizeof(*group));
        group->groupName = SkalStrdup(groupName);
        group->threadSubscribers = CdsListCreate(NULL, 0,
                skaldThreadSubscriberUnref);
        group->skaldSubscribers = CdsListCreate(NULL, 0,
                skaldSkaldSubscriberUnref);
        bool inserted = CdsMapInsert(gGroups,
                group->groupName, (CdsMapItem*)group);
        SKALASSERT(inserted);
    }
    return group;
}


static void skaldGroupSubscribeThread(const char* groupName,
        const char* threadName, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(SkalIsAsciiString(threadName));
    skaldGroup* group = skaldGetGroup(groupName);

    // First, ensure that this subscriber does not exist already
    CDSLIST_FOREACH(group->threadSubscribers,
            skaldThreadSubscriber, subscriber) {
        if (    (SkalStrcmp(subscriber->threadName, threadName) == 0)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            return; // Subscriber already exists, nothing to do
        }
    }

    skaldThreadSubscriber* subscriber = SkalMallocZ(sizeof(*subscriber));
    subscriber->threadName = SkalStrdup(threadName);
    subscriber->pattern = SkalStrdup(pattern);
    if (SkalStartsWith(subscriber->pattern, "regex:")) {
        subscriber->regex = SkalPlfRegexCreate(subscriber->pattern + 6);
        if (NULL == subscriber->regex) {
            SkalLog("SKALD: Received a skal-subscribe message from '%s' with an invalid regex '%s'; request ignored",
                    threadName, subscriber->pattern + 6);
            skaldThreadSubscriberUnref(&subscriber->item);
            return;
        }
    }
    bool inserted = CdsListPushBack(group->threadSubscribers,
            (CdsListItem*)subscriber);
    SKALASSERT(inserted);

    // TODO: Inform other domain skalds
}


void skaldGroupUnsubscribeThread(const char* groupName,
        const char* threadName, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(SkalIsAsciiString(threadName));

    skaldGroup* group = (skaldGroup*)CdsMapSearch(gGroups, (void*)groupName);
    if (NULL == group) {
        return; // This group does not exist => nothing to do
    }

    CDSLIST_FOREACH(group->threadSubscribers,
            skaldThreadSubscriber, subscriber) {
        if (    (SkalStrcmp(subscriber->threadName, threadName) == 0)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            // TODO: Notify domain skalds
            CdsListRemove((CdsListItem*)subscriber);
            skaldThreadSubscriberUnref(&subscriber->item);
            if (    CdsListIsEmpty(group->threadSubscribers)
                 && CdsListIsEmpty(group->skaldSubscribers)) {
                CdsMapItemRemove(gGroups, (CdsMapItem*)group);
            }
            break;
        }
    }
}


#if 0
static void skaldGroupSubscribeSkald(const char* groupName,
        int sockid, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(sockid >= 0);
    skaldGroup* group = skaldGetGroup(groupName);

    // First, ensure that this subscriber does not exist already
    CDSLIST_FOREACH(group->skaldSubscribers,
            skaldSkaldSubscriber, subscriber) {
        if (    (subscriber->sockid == sockid)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            return; // Subscriber already exists, nothing to do
        }
    }

    skaldSkaldSubscriber* subscriber = SkalMallocZ(sizeof(*subscriber));
    subscriber->sockid = sockid;
    subscriber->pattern = SkalStrdup(pattern);
    if (SkalStartsWith(subscriber->pattern, "regex:")) {
        subscriber->regex = SkalPlfRegexCreate(subscriber->pattern + 6);
        if (NULL == subscriber->regex) {
            SkalLog("SKALD: Received a skald-subscribe message with an invalid regex '%s'; request ignored",
                    subscriber->pattern + 6);
            skaldSkaldSubscriberUnref(&subscriber->item);
            return;
        }
    }
    bool inserted = CdsListPushBack(group->skaldSubscribers,
            (CdsListItem*)subscriber);
    SKALASSERT(inserted);
}


static void skaldGroupUnsubscribeSkald(const char* groupName,
        int sockid, const char* pattern)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(sockid >= 0);

    skaldGroup* group = (skaldGroup*)CdsMapSearch(gGroups, (void*)groupName);
    if (NULL == group) {
        return; // This group does not exist => nothing to do
    }

    CDSLIST_FOREACH(group->skaldSubscribers, skaldSkaldSubscriber, subscriber) {
        if (    (subscriber->sockid == sockid)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            CdsListRemove((CdsListItem*)subscriber);
            skaldSkaldSubscriberUnref(&subscriber->item);
            if (    CdsListIsEmpty(group->threadSubscribers)
                 && CdsListIsEmpty(group->skaldSubscribers)) {
                CdsMapItemRemove(gGroups, (CdsMapItem*)group);
            }
            break;
        }
    }
}
#endif


static void skaldGroupUnsubscribeAllThreads(skaldSocketCtx* ctx)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(ctx->threadNames != NULL);

    CdsMapIteratorReset(ctx->threadNames, true);
    for (CdsMapItem* item = CdsMapIteratorNext(ctx->threadNames, NULL);
            item != NULL;
            item = CdsMapIteratorNext(ctx->threadNames, NULL)) {
        skaldNameMapItem* nameItem = (skaldNameMapItem*)item;
        const char* threadName = nameItem->name;
        CdsMapIteratorReset(gGroups, true);
        for (CdsMapItem* item2 = CdsMapIteratorNext(gGroups, NULL);
                item2 != NULL;
                item2 = CdsMapIteratorNext(gGroups, NULL)) {
            skaldGroup* group = (skaldGroup*)item2;
            CDSLIST_FOREACH(group->threadSubscribers,
                    skaldThreadSubscriber, subscriber) {
                if (SkalStrcmp(subscriber->threadName, threadName) == 0) {
                    // TODO: Inform domain skalds
                    CdsListRemove((CdsListItem*)subscriber);
                    skaldThreadSubscriberUnref(&subscriber->item);
                }
            } // for each thread subcriber
        } // for each group
    } // for each dead thread
}


static void skaldMulticastDispatch(const char* groupName, const SkalMsg* msg)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(msg != NULL);

    skaldGroup* group = (skaldGroup*)CdsMapSearch(gGroups, (void*)groupName);
    if (NULL == group) {
        return; // Nobody's listening to that group => nothing to do
    }

    const char* msgName = SkalMsgName(msg);
    CDSLIST_FOREACH(group->threadSubscribers,
            skaldThreadSubscriber, subscriber) {
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
            SkalMsg* copy = SkalMsgCopy(msg, subscriber->threadName);
            skaldRouteMsg(copy);
        }
    } // for each thread subscriber

    CDSLIST_FOREACH(group->skaldSubscribers, skaldSkaldSubscriber, subscriber) {
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
            // TODO: send to other skald
        }
    } // for each thread subscriber
}
