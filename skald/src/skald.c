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


/** Structure that represents a thread
 *
 * This goes into `skaldCtx.threads`.
 */
typedef struct {
    CdsMapItem item;
    int        ref;         /**< Reference counter */
    char*      threadName;  /**< Thread name; also the map key */
} skaldThread;


/** Structure used to lookup a socket id for a thread
 *
 * This goes into the `gThreadLookup` map.
 */
typedef struct {
    CdsMapItem item;
    int        ref;        /**< Reference counter */
    int        sockid;     /**< Socket of process/skald this thread is part of*/
    char*      threadName; /**< Thread name; also the map key */
} skaldThreadLookup;


/** Structure that holds information about a group subscriber which is a thread
 *
 * A subscription is uniquely identified by the pair "thread name + pattern".
 * This allows the same thread to subscribe mulitple times with different
 * patterns.
 *
 * Please note a domain thread can't be a subscriber. A domain thread must
 * subscribe to its own skald.
 */
typedef struct {
    CdsListItem item;

    /** Reference counter */
    int ref;

    /** Name of the thread that subscribed to this group */
    char* threadName;

    /** Socket to use to send data to this subscriber thread */
    int sockid;

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
 */
typedef struct {
    CdsListItem item;

    /** Reference counter */
    int ref;

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


/** Structure that holds information about a group */
// TODO: refactor groups: use `pattern` as the first key, as this will make
// dispatching multicast messages faster.
typedef struct {
    CdsMapItem item;

    /** Reference counter */
    int ref;

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


/** Structure that holds information related to a socket */
typedef struct {
    /** Reference counter */
    int ref;

    /** Socket id
     *
     * This is redundant because the context is tied to a skal-net socket, but
     * it's useful to have it around.
     */
    int sockid;

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

    /** Map of threads - made of `skaldThread`
     *
     * These are the threads that live on the other side of this socket. They
     * may be in a process or in a skald in the same domain.
     *
     * Meaningful only if `type` is `SKALD_SOCKET_DOMAIN_SKALD` or
     * `SKALD_SOCKET_PROCESS`; stays empty for all other types.
     */
    CdsMap* threads;

    /** Whether the previous send failed
     *
     * This is used to manage the `skal-io-send-fail` alarm.
     */
    bool sendFail;
} skaldCtx;



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


/** De-reference a `skaldThread` */
static void skaldThreadUnref(CdsMapItem* item);


/** De-reference a `skaldGroup` */
static void skaldGroupUnref(CdsMapItem* item);


/** De-reference a `skaldThreadSubscriber` */
static void skaldThreadSubscriberUnref(CdsListItem* item);


/** De-reference a `skaldSkaldSubscriber` */
static void skaldSkaldSubscriberUnref(CdsListItem* item);


/** De-reference a `skaldCtx` */
static void skaldCtxUnref(void* context);


/** Function to de-reference a `skaldThreadLookup` */
static void skaldThreadLookupUnref(CdsMapItem* mitem);


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


/** Wrapper around `SkalNetSend_BLOCKING()`
 *
 * This function tries to send `json` over the given socket, and raises an alarm
 * if a failure occurs.
 *
 * If the send succeeds, this function returns `true`.
 *
 * If the send fails but the socket is still alive (eg: the send failed because
 * the socket buffer was full), this function does not send the message and
 * returns `false`.
 *
 * If the send fails and the socket is not useable anymore (eg: connection reset
 * by peer), this function does not send the message, closes the socket, and
 * returns `false`.
 *
 * @param sockid [in] Identifier of socket where to send the data
 * @param json   [in] Data to send; must not be NULL; must be a valid UTF-8,
 *                    null-terminated string
 *
 * @return `true` if success, `false` if failure
 */
static bool skaldSendOnSocket(int sockid, const char* json);


/** Send a message through the given socket
 *
 * This function takes ownership of `msg`.
 *
 * @param msg    [in,out] Message to send; must not be NULL
 * @param sockid [in]     On which socket to send this message through
 */
static void skaldMsgSendTo(SkalMsg* msg, int sockid);


/** Handle a connection request
 *
 * A socket context will be created and assigned to `sockid`; the context type
 * will be set to SKALD_SOCKET_UNDETERMINED.
 *
 * @param sockid [in] Id of new socket
 */
static void skaldHandleCnxRequest(int sockid);


/** Handle data coming from a socket
 *
 * @param ctx    [in,out] Context of socket where data has been received; must
 *                        not be NULL
 * @param data   [in,out] Pointer to data; must not be NULL; may be modified
 * @param size_B [in]     Number of bytes received; must be >0
 */
static void skaldHandleDataIn(skaldCtx* ctx, uint8_t* data, int64_t size_B);


/** Handle a message received from someone who just connected
 *
 * This function takes ownership of `msg`.
 *
 * @param ctx [in,out] Socket context; must not be NULL; must be of type
 *                     SKALD_SOCKET_UNDETERMINED
 * @param msg [in]     Message received; must not be NULL
 */
static void skaldHandleDataInFromUndetermined(skaldCtx* ctx, SkalMsg* msg);


/** Take action on an incoming message from a process
 *
 * This function takes ownership of `msg`.
 *
 * @param ctx [in,out] Context of socket that received this message; must not
 *                     be NULL, must be of type SKALD_SOCKET_PROCESS
 * @param msg [in]     Message to process; must not be NULL
 */
static void skaldHandleMsgFromProcess(skaldCtx* ctx, SkalMsg* msg);


/** Take action on an incoming message for this skald from a process
 *
 * This function takes ownership of `msg`.
 *
 * @param ctx [in,out] Context of socket that received this message; must not
 *                     be NULL, must be of type SKALD_SOCKET_PROCESS
 * @param msg [in]     Message to process; must not be NULL
 */
static void skaldProcessMsgFromProcess(skaldCtx* ctx, SkalMsg* msg);


/** Get the object representing the given group, creating it if necessary
 *
 * @param groupName [in] The group name; must not be NULL
 *
 * @return The group structure; this function never returns NULL
 */
static skaldGroup* skaldGetOrCreateGroup(const char* groupName);


/** Delete group if nobody is listening to it
 *
 * @param group [in] The group to check; must not be NULL
 *
 * @return `true` if the group has been deleted, `false` if it still has at
 *         least one subscriber
 */
static bool skaldDeleteGroupIfNoListener(skaldGroup* group);


/** Add a thread+pattern subscriber to the given group
 *
 * @param groupName  [in] Name of the group where to add the thread subscriber;
 *                        must not be NULL
 * @param threadName [in] Name of the thread that wants to subscribe; must be a
 *                        valid thread name
 * @param pattern    [in] Pattern to match; may be NULL
 * @param sockid     [in] Socket where to send data to this subscriber thread
 *
 * The `pattern` parameter may be one of the following:
 *  - NULL or the empty string: the thread will be forwarded all messages sent
 *    to this group
 *  - A string that starts with "regex:": the thread will be forwarded all
 *    messages whose names match the regular expression
 *  - Otherwise, the thread will be forwarded all messages whose names start
 *    with the given string
 */
static void skaldGroupSubscribeThreadPattern(const char* groupName,
        const char* threadName, const char* pattern, int sockid);


/** Remove a thread+pattern subscriber from the given group
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
static void skaldGroupUnsubscribeThreadPattern(const char* groupName,
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


/** Remove all subscriptions for the given thread
 *
 * @param threadName [in] Name of thread to unsubscribe
 */
static void skaldGroupUnsubscribeThread(const char* threadName);


/** Check if the message name is a match for the given `regex` or `pattern` */
static bool skaldMulticastIsMatch(const char* msgName,
        const SkalPlfRegex* regex, const char* pattern);


/** Send the given message to all subscribers of the given multicast group
 *
 * The ownership of `msg` stays with the caller. For thread subscribers, the
 * message will be forwarded only if its name matches the subscriber pattern.
 *
 * @param groupName [in] Name of the group; must not be NULL
 * @param msg       [in] Message to send; must not be NULL
 */
static void skaldMulticastDispatch(const char* groupName, const SkalMsg* msg);


/** Send the given message to all subscribers of the "skal-trace" group
 *
 * @param msg  [in] Message to trace; must not be NULL
 * @param json [in] JSON representation of `msg`; may be NULL; if not NULL, it
 *                  is assumed it is the representation of `msg` in JSON format,
 *                  this is an optimisation
 */
static void skaldTrace(const SkalMsg* msg, const char* json);



/*------------------+
 | Global variables |
 +------------------*/


/** SKALD thread */
static SkalPlfThread* gSkaldThread = NULL;


/** Sockets
 *
 * We use the skal-net ability to hold cookies for each socket to store
 * information related to each one of those sockets; cookie are thus of type
 * `skaldCtx`.
 */
static SkalNet* gNet = NULL;


/** Repeat pipe client sockid for easy access */
static int gPipeClientSockid = -1;


/** Map to quickly lookup a socket id from a thread name
 *
 * Map is made of `skaldThreadLookup`, and the key is the thread name. All
 * managed and domain threads are listed here (but not foreign threads, because
 * this might make the map too big for very large applications).
 */
static CdsMap* gThreadLookup = NULL;


/** Information about groups
 *
 * Map is made of `skaldGroup`, and the key is the group name.
 */
static CdsMap* gGroups = NULL;


/** The "skal-trace" group
 *
 * We need to handle this group separately because when a message is sent to a
 * regular multicast group, its recipient is overwritten with the group's
 * subscribers. We want to trace the intact message so we do it using the JSON
 * format as it is received or sent.
 *
 * Additionally, because it's a group with a lot of traffic, handling it
 * separately will bring some optimisations.
 *
 * The `gTraceGroup` variable points to the "skal-trace" group inside the
 * `gGroups`, or NULL if nobody's listening to the "skal-trace" group.
 */
static skaldGroup* gTraceGroup = NULL;


/** The full name of this skald "thread" */
static char* gName = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldRun(const SkaldParams* params)
{
    SKALASSERT(NULL == gSkaldThread);
    SKALASSERT(NULL == gNet);
    SKALASSERT(-1 == gPipeClientSockid);
    SKALASSERT(NULL == gThreadLookup);
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

    gThreadLookup = CdsMapCreate("threadLookup",          // name
                                 0,                       // capacity
                                 SkalStringCompare,       // compare
                                 NULL,                    // cookie
                                 NULL,                    // keyUnref
                                 skaldThreadLookupUnref); // itemUnref

    // Create pipe to allow skald to terminate cleanly
    skaldCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_SERVER;
    ctx->name = SkalStrdup("pipe-server");
    ctx->sockid = SkalNetServerCreate(gNet, "pipe://", 0, ctx, 0);
    SKALASSERT(ctx->sockid >= 0);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(ctx->sockid == event->sockid);
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_CLIENT;
    ctx->name = SkalStrdup("pipe-client");
    ctx->sockid = event->conn.commSockid;
    bool contextSet = SkalNetSetContext(gNet, ctx->sockid, ctx);
    SKALASSERT(contextSet);
    gPipeClientSockid = event->conn.commSockid;
    SkalNetEventUnref(event);

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_SERVER;
    ctx->name = SkalStrdup("local-server");
    ctx->sockid = SkalNetServerCreate(gNet, localUrl, 0, ctx, 0);
    if (ctx->sockid < 0) {
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

    CdsMapDestroy(gGroups);
    CdsMapDestroy(gThreadLookup);
    SkalNetDestroy(gNet);
    free(gName);

    gNet = NULL;
    gPipeClientSockid = -1;
    gThreadLookup = NULL;
    gTraceGroup = NULL;
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

        skaldCtx* ctx = (skaldCtx*)(event->context);
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
                skaldHandleCnxRequest(event->conn.commSockid);
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
                SkaldAlarmNew("skal-io-socket-error",
                        SKAL_ALARM_ERROR, true, false,
                        "Error reported on socket '%s'", ctx->name);
                // fallthrough
            case SKAL_NET_EV_DISCONN :
                SkalNetSocketDestroy(gNet, event->sockid);
                break;
            case SKAL_NET_EV_IN :
                skaldHandleDataIn(ctx, event->in.data, event->in.size_B);
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
                SkalNetSocketDestroy(gNet, event->sockid);
                break;
            case SKAL_NET_EV_IN :
                skaldHandleDataIn(ctx, event->in.data, event->in.size_B);
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


static void skaldThreadUnref(CdsMapItem* item)
{
    SKALASSERT(item != NULL);
    skaldThread* thread = (skaldThread*)item;
    (thread->ref)--;
    if (thread->ref <= 0) {
        // This thread is being removed from the thread map of the containing
        // process context. This means that this thread does not exist anymore.
        // We must maintain our internal structures thus:
        //  - Remove any group subscriptions for that thread
        //  - Remove the thread lookup from the `gThreadLookup` map
        skaldGroupUnsubscribeThread(thread->threadName);
        CdsMapRemove(gThreadLookup, thread->threadName);
        free(thread->threadName);
        free(thread);
    }
}


static void skaldGroupUnref(CdsMapItem* item)
{
    SKALASSERT(item != NULL);
    skaldGroup* group = (skaldGroup*)item;
    (group->ref)--;
    if (group->ref <= 0) {
        if (group->threadSubscribers != NULL) {
            CdsListDestroy(group->threadSubscribers);
        }
        if (group->skaldSubscribers != NULL) {
            CdsListDestroy(group->skaldSubscribers);
        }
        free(group->groupName);
        free(group);
    }
}


static void skaldThreadSubscriberUnref(CdsListItem* item)
{
    SKALASSERT(item != NULL);
    skaldThreadSubscriber* subscriber = (skaldThreadSubscriber*)item;
    (subscriber->ref)--;
    if (subscriber->ref <= 0) {
        free(subscriber->threadName);
        free(subscriber->pattern);
        if (subscriber->regex != NULL) {
            SkalPlfRegexDestroy(subscriber->regex);
        }
        free(subscriber);
    }
}


static void skaldSkaldSubscriberUnref(CdsListItem* item)
{
    SKALASSERT(item != NULL);
    skaldSkaldSubscriber* subscriber = (skaldSkaldSubscriber*)item;
    (subscriber->ref)--;
    if (subscriber->ref <= 0) {
        free(subscriber->pattern);
        if (subscriber->regex != NULL) {
            SkalPlfRegexDestroy(subscriber->regex);
        }
        free(subscriber);
    }
}


static void skaldCtxUnref(void* context)
{
    SKALASSERT(context != NULL);
    skaldCtx* ctx = (skaldCtx*)context;
    (ctx->ref)--;
    if (ctx->ref <= 0) {
        if (ctx->threads != NULL) {
            CdsMapDestroy(ctx->threads);
            // NB: Destroying the map will unreference the threads it contains.
            // When a thread is unreferenced, it will clean up the group
            // structures and `gThreadLookup` accordingly.
        }
        free(ctx->name);
        free(ctx->domain);
        free(ctx);
    }
}


static void skaldThreadLookupUnref(CdsMapItem* item)
{
    SKALASSERT(item != NULL);
    skaldThreadLookup* lookup = (skaldThreadLookup*)item;
    (lookup->ref)--;
    if (lookup->ref <= 0) {
        // NB: When a thread is removed from the `gThreadLookup` map, is has
        // already been removed from all groups and from the corresponding
        // socket context. So there is no more maintenance to do here except for
        // freeing the item.
        free(lookup->threadName);
        free(lookup);
    }
}


static void skaldGroupUnsubscribeThread(const char* threadName)
{
    for (   CdsMapItem* item = CdsMapIteratorStart(gGroups, true, NULL);
            item != NULL;
            item = CdsMapIteratorNext(gGroups, NULL)) {
        skaldGroup* group = (skaldGroup*)item;
        CDSLIST_FOREACH(group->threadSubscribers,
                skaldThreadSubscriber, subscriber) {
            if (SkalStrcmp(subscriber->threadName, threadName) == 0) {
                CdsListRemove((CdsListItem*)subscriber);
                skaldThreadSubscriberUnref((CdsListItem*)subscriber);
            }
            if (skaldDeleteGroupIfNoListener(group)) {
                break;
            }
        }
    }
}


static void skaldHandleCnxRequest(int sockid)
{
    skaldCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->ref = 1;
    ctx->sockid = sockid;
    ctx->type = SKALD_SOCKET_UNDETERMINED;
    ctx->name = SkalSPrintf("undetermined (%d)", ctx->sockid);
    bool contextSet = SkalNetSetContext(gNet, sockid, ctx);
    SKALASSERT(contextSet);
}


static void skaldHandleDataIn(skaldCtx* ctx, uint8_t* data, int64_t size_B)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);

    char* json = (char*)data;
    // The string normally arrives null-terminated, but it's safer to enforce
    // null termination
    json[size_B - 1] = '\0';
    SkalMsg* msg = SkalMsgCreateFromJson(json);
    if (NULL == msg) {
        SkaldAlarmNew("skal-protocol-invalid-json",
                SKAL_ALARM_ERROR, true, false,
                "From connection '%s'", ctx->name);
    } else {
        skaldTrace(msg, json);
        switch (ctx->type) {
        case SKALD_SOCKET_UNDETERMINED :
            skaldHandleDataInFromUndetermined(ctx, msg);
            break;
        case SKALD_SOCKET_PROCESS :
            skaldHandleMsgFromProcess(ctx, msg);
            break;
        case SKALD_SOCKET_DOMAIN_SKALD :
            SKALPANIC_MSG("Not yet implemented");
            break;
        default :
            SKALPANIC_MSG("Received data on socket '%s', but socket is of the wrong type %d",
                    ctx->name, (int)ctx->type);
            break;
        }
    }
}


static void skaldHandleDataInFromUndetermined(skaldCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_UNDETERMINED == ctx->type);
    SKALASSERT(NULL == ctx->threads);
    SKALASSERT(msg != NULL);

    const char* msgName = SkalMsgName(msg);
    if (SkalStartsWith(msgName, "skal-init-")) {
        // The peer is a process
        ctx->type = SKALD_SOCKET_PROCESS;
        free(ctx->name);
        ctx->name = SkalSPrintf("process (%d)", ctx->sockid);
        ctx->threads = CdsMapCreate(NULL,              // name
                                    0,                 // capacity
                                    SkalStringCompare, // compare
                                    NULL,              // cookie
                                    NULL,              // keyUnref
                                    skaldThreadUnref); // itemUnref
        skaldHandleMsgFromProcess(ctx, msg);

    } else if (SkalStartsWith(msgName, "skald-init-")) {
        // The peer is a skald
        // TODO
        SkalMsgUnref(msg);

    } else {
        SkaldAlarmNew("skal-protocol-invalid-msg", SKAL_ALARM_ERROR,
                true, false,
                "From socket '%s'; expected 'skal-init-' or 'skald-init-'",
                ctx->name);
        SkalMsgUnref(msg);
    }
}


static void skaldHandleMsgFromProcess(skaldCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    SKALASSERT(msg != NULL);

    // Basic checks on `msg`
    const char* msgName = SkalMsgName(msg);
    SKALASSERT(msgName != NULL);
    const char* senderName = SkalMsgSender(msg);
    SKALASSERT(senderName != NULL);
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
    SKALASSERT(recipientName != NULL);
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

    if (    (SkalStrcmp(recipientName, gName) == 0)
         || (SkalStartsWith(msgName, "skal-init-"))) {
        // The recipient of this message is this skald.
        //
        // Please note if the message name starts with "skal-init-", it is part
        // of the SKAL protocol where a process connects to us. So such a
        // message is always for the skald which is local to the process, even
        // if the recipient domain is not set correctly (because the process
        // doesn't know yet what is its domain).
        skaldProcessMsgFromProcess(ctx, msg);
        return;
    }

    // Special treatment for `skal-ntf-xon` messages: ensure the sender is not
    // blocked on a non-existing recipient.
    if (SkalStrcmp(msgName, "skal-ntf-xon") == 0) {
        const char* domain = skaldDomain(recipientName);
        SKALASSERT(domain != NULL);
        if (    (SkalStrcmp(domain, SkalDomain()) == 0)
             && (CdsMapSearch(gThreadLookup, (void*)recipientName) == NULL)) {
            // The recipient of this `skal-ntf-xon` does not exist anymore
            //  => Unblock the sender and don't forward the original msg
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

    // If we are here, the message is not for me and should be routed
    // accordingly.
    skaldRouteMsg(msg);
}


static void skaldProcessMsgFromProcess(skaldCtx* ctx, SkalMsg* msg)
{
    SKALASSERT(ctx != NULL);
    SKALASSERT(SKALD_SOCKET_PROCESS == ctx->type);
    SKALASSERT(msg != NULL);

    // Take action depending on message name
    const char* msgName = SkalMsgName(msg);
    SKALASSERT(msgName != NULL);
    const char* senderName = SkalMsgSender(msg);
    SKALASSERT(senderName != NULL);
    if (SkalStrcmp(msgName, "skal-init-master-born") == 0) {
        // The `skal-master` thread of a process is uttering its first words!
        if (!SkalMsgHasAsciiString(msg, "name")) {
            SkaldAlarmNew("skal-protocol-missing-field",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-init-master-born' message from '%s' without a 'name' field",
                    senderName);

        } else {
            const char* name = SkalMsgGetString(msg, "name");
            free(ctx->name);
            ctx->name = SkalStrdup(name);

            // Respond with the domain name
            SkalMsg* resp = SkalMsgCreate("skal-init-domain", "skal-master");
            SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
            SkalMsgAddString(resp, "domain", SkalDomain());
            skaldMsgSendTo(resp, ctx->sockid);
        }

    } else if (SkalStrcmp(msgName, "skal-born") == 0) {
        // A managed or domain thread has been born
        const char* domain = skaldDomain(senderName);
        SKALASSERT(domain != NULL);
        if (SkalStrcmp(domain, SkalDomain()) != 0) {
            SkaldAlarmNew("skal-protocol-wrong-sender-domain",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', which is on a different domain than mine (%s)",
                    senderName, SkalDomain());

        } else if (CdsMapSearch(gThreadLookup, (void*)senderName) != NULL) {
            SkaldAlarmNew("skal-conflict-duplicate-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a 'skal-born' message from '%s', but a thread with that name is already registered",
                    senderName);

        } else if (CdsMapSearch(ctx->threads, (void*)senderName) != NULL) {
            SkaldAlarmNew("skal-internal",
                    SKAL_ALARM_ERROR, true, false,
                    "Thread '%s' not registered globally, but is listed for process '%s'; this is impossible",
                    senderName, ctx->name);

        } else {
            skaldThread* thread = SkalMallocZ(sizeof(*thread));
            thread->ref = 1;
            thread->threadName = SkalStrdup(senderName);
            bool inserted = CdsMapInsert(ctx->threads,
                    thread->threadName, (CdsMapItem*)thread);
            SKALASSERT(inserted);

            skaldThreadLookup* lookup = SkalMallocZ(sizeof(*lookup));
            lookup->ref = 1;
            lookup->sockid = ctx->sockid;
            lookup->threadName = SkalStrdup(senderName);
            inserted = CdsMapInsert(gThreadLookup,
                    lookup->threadName, (CdsMapItem*)lookup);
            SKALASSERT(inserted);

            if (SKALD_SOCKET_PROCESS == ctx->type) {
                // TODO: inform all domain peers
            }
        }

    } else if (SkalStrcmp(msgName, "skal-died") == 0) {
        // A managed or domain thread just died
        // NB: When unreferenced, the thread will clean up the other data
        // structures
        bool removed = CdsMapRemove(ctx->threads, (void*)senderName);
        if (!removed) {
            SkaldAlarmNew("skal-conflict-unknown-thread",
                    SKAL_ALARM_WARNING, true, false,
                    "Received 'skal-died' for unknown thread '%s'",
                    senderName);
        }

        if (SKALD_SOCKET_PROCESS == ctx->type) {
            // TODO: inform domain peers
        }

    } else if (SkalStrcmp(msgName, "skal-ping") == 0) {
        SkalMsg* resp = SkalMsgCreate("skal-pong", senderName);
        SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
        skaldRouteMsg(resp);

    } else if (SkalStrcmp(msgName, "skal-subscribe") == 0) {
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
                SkaldAlarmNew("skal-protocol-subscribe-wrong-domain",
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
                skaldGroupSubscribeThreadPattern(tmp,
                        senderName, pattern, ctx->sockid);
                free(tmp);
            }
        }

    } else if (SkalStrcmp(msgName, "skal-unsubscribe") == 0) {
        // The sender wants to unsubscribe from a group
        if (!SkalMsgHasAsciiString(msg, "group")) {
            SkaldAlarmNew("skal-protocol-unsubscribe-without-group",
                    SKAL_ALARM_WARNING, true, false,
                    "Received a skal-unsubscribe message from '%s' without a 'group' field",
                    senderName);
        } else {
            const char* groupName = SkalMsgGetString(msg, "group");
            const char* domain = skaldDomain(groupName);
            if ((domain != NULL) && (SkalStrcmp(domain, SkalDomain()) != 0)) {
                SkaldAlarmNew("skal-protocol-unsubscribe-wrong-domain",
                        SKAL_ALARM_WARNING, true, false,
                        "Received a skal-unsubscribe message from '%s' for group '%s' which is not in my domain (%s); request ignored",
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
                skaldGroupUnsubscribeThreadPattern(tmp, senderName, pattern);
                free(tmp);
            }
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

    const char* reasonStr = NULL;
    char* extraStr = NULL;
    switch (reason) {
    case SKALD_DROP_TTL :
        SkaldAlarmNew("skal-drop-ttl", SKAL_ALARM_WARNING, true, false,
                "Message '%s' TTL has reached 0; message dropped",
                SkalMsgName(msg));
        reasonStr = "ttl-expired";
        break;
    case SKALD_DROP_NO_RECIPIENT :
        SkaldAlarmNew("skal-drop", SKAL_ALARM_WARNING, true, false,
                "Can't route message '%s' because I know nothing about its recipient '%s'; message dropped",
                SkalMsgName(msg), SkalMsgRecipient(msg));
        reasonStr = "no-recipient";
        extraStr = SkalSPrintf("Thread '%s' does not exist",
                SkalMsgRecipient(msg));
        break;
    default :
        SKALPANIC_MSG("Unknown drop reason: %d", (int)reason);
    }

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
    SKALASSERT(recipientName != NULL);
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

    } else if (SkalStrcmp(domain, SkalDomain()) == 0) {
        skaldThreadLookup* lookup = (skaldThreadLookup*)CdsMapSearch(
                gThreadLookup, (void*)recipientName);
        if (lookup != NULL) {
            skaldMsgSendTo(msg, lookup->sockid);
        } else if (SkalStrcmp(recipientName, gName) == 0) {
            SkaldAlarmNew("skal-conflict-circular-msg",
                    SKAL_ALARM_ERROR, true, false,
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


static bool skaldSendOnSocket(int sockid, const char* json)
{
    bool sent = false;
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            sockid, json, strlen(json) + 1);
    SKALASSERT(result != SKAL_NET_SEND_INVAL_SOCKID);

    skaldCtx* ctx = SkalNetGetContext(gNet, sockid);
    SKALASSERT(ctx != NULL);
    switch (result) {
    case SKAL_NET_SEND_OK :
        sent = true;
        if (ctx->sendFail) {
            SkaldAlarmNew("skal-io-send-fail", SKAL_ALARM_WARNING, false, true,
                    "Can send over socket '%s' again", ctx->name);
            ctx->sendFail = false;
        }
        break;

    case SKAL_NET_SEND_TOO_BIG :
    case SKAL_NET_SEND_TRUNC :
        SkaldAlarmNew("skal-io-send-fail", SKAL_ALARM_WARNING, true, true,
                "Failed to send over socket '%s' (socket still alive)",
                ctx->name);
        ctx->sendFail = true;
        break;

    case SKAL_NET_SEND_RESET :
        SkaldAlarmNew("skal-io-send-fail-reset", SKAL_ALARM_ERROR, true, false,
                "Failed to send over socket '%s' (closed by peer)", ctx->name);
        SkalNetSocketDestroy(gNet, sockid);
        break;

    default :
        SkaldAlarmNew("skal-io-send-fail-error", SKAL_ALARM_ERROR, true, false,
                "Failed to send over socket '%s' (general error)", ctx->name);
        SkalNetSocketDestroy(gNet, sockid);
        break;
    }
    return sent;
}


static void skaldMsgSendTo(SkalMsg* msg, int sockid)
{
    SKALASSERT(msg != NULL);
    skaldCtx* ctx = SkalNetGetContext(gNet, sockid);
    SKALASSERT(ctx != NULL);

    switch (ctx->type) {
    case SKALD_SOCKET_DOMAIN_SKALD :
    case SKALD_SOCKET_FOREIGN_SKALD :
        SKALPANIC_MSG("SKALD comms not yet implemented");
        break;
    case SKALD_SOCKET_PROCESS :
        {
            char* json = SkalMsgToJson(msg);
            if (skaldSendOnSocket(sockid, json)) {
                skaldTrace(msg, json);
            }
            free(json);
        }
        break;
    default :
        SKALPANIC_MSG("Can't send a msg over socket type %d", (int)ctx->type);
    }
    SkalMsgUnref(msg);
}


static skaldGroup* skaldGetOrCreateGroup(const char* groupName)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(gGroups != NULL);
    skaldGroup* group = (skaldGroup*)CdsMapSearch(gGroups, (void*)groupName);
    if (NULL == group) {
        group = SkalMallocZ(sizeof(*group));
        group->ref = 1;
        group->groupName = SkalStrdup(groupName);
        group->threadSubscribers = CdsListCreate(NULL, 0,
                skaldThreadSubscriberUnref);
        group->skaldSubscribers = CdsListCreate(NULL, 0,
                skaldSkaldSubscriberUnref);
        bool inserted = CdsMapInsert(gGroups,
                group->groupName, (CdsMapItem*)group);
        SKALASSERT(inserted);

        if (SkalStartsWith(groupName, "skal-trace")) {
            gTraceGroup = group;
        }
    }
    return group;
}


static bool skaldDeleteGroupIfNoListener(skaldGroup* group)
{
    SKALASSERT(group != NULL);
    SKALASSERT(group->threadSubscribers != NULL);
    SKALASSERT(group->skaldSubscribers != NULL);
    bool deleted = false;
    if (    CdsListIsEmpty(group->threadSubscribers)
         && CdsListIsEmpty(group->skaldSubscribers)) {
        if (gTraceGroup == group) {
            gTraceGroup = NULL;
        }
        CdsMapItemRemove(gGroups, (CdsMapItem*)group);
        deleted = true;
    }
    return deleted;
}


static void skaldGroupSubscribeThreadPattern(const char* groupName,
        const char* threadName, const char* pattern, int sockid)
{
    SKALASSERT(SkalIsAsciiString(groupName));
    SKALASSERT(SkalIsAsciiString(threadName));
    skaldGroup* group = skaldGetOrCreateGroup(groupName);

    // First, ensure that this subscriber does not exist already
    CDSLIST_FOREACH(group->threadSubscribers,
            skaldThreadSubscriber, subscriber) {
        if (    (SkalStrcmp(subscriber->threadName, threadName) == 0)
             && (SkalStrcmp(subscriber->pattern, pattern) == 0)) {
            return; // Subscriber already exists, nothing to do
        }
    }

    skaldThreadSubscriber* subscriber = SkalMallocZ(sizeof(*subscriber));
    subscriber->ref = 1;
    subscriber->threadName = SkalStrdup(threadName);
    subscriber->sockid = sockid;
    subscriber->pattern = SkalStrdup(pattern);
    if (SkalStartsWith(subscriber->pattern, "regex:")) {
        subscriber->regex = SkalPlfRegexCreate(subscriber->pattern + 6);
        if (NULL == subscriber->regex) {
            SkalLog("SKALD: Received a skal-subscribe message from '%s' with an invalid regex '%s'; request ignored",
                    threadName, subscriber->pattern + 6);
            skaldThreadSubscriberUnref((CdsListItem*)subscriber);
            return;
        }
    }
    bool inserted = CdsListPushBack(group->threadSubscribers,
            (CdsListItem*)subscriber);
    SKALASSERT(inserted);

    // TODO: Inform other domain skalds
}


static void skaldGroupUnsubscribeThreadPattern(const char* groupName,
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
            skaldThreadSubscriberUnref((CdsListItem*)subscriber);
            (void)skaldDeleteGroupIfNoListener(group);
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
    skaldGroup* group = skaldGetOrCreateGroup(groupName);

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
            (void)skaldDeleteGroupIfNoListener(group);
            break;
        }
    }
}
#endif


static bool skaldMulticastIsMatch(const char* msgName,
        const SkalPlfRegex* regex, const char* pattern)
{
    if (regex != NULL) {
        if (SkalPlfRegexRun(regex, msgName)) {
            return true;
        }
    }
    if (pattern != NULL) {
        return SkalStartsWith(msgName, pattern);
    }
    return true;
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
        if (skaldMulticastIsMatch(msgName,
                    subscriber->regex, subscriber->pattern)) {
            SkalMsg* copy = SkalMsgCopy(msg, subscriber->threadName);
            char* json = SkalMsgToJson(copy);
            (void)skaldSendOnSocket(subscriber->sockid, json);
            free(json);
            SkalMsgUnref(copy);
        }
    } // for each thread subscriber

    CDSLIST_FOREACH(group->skaldSubscribers, skaldSkaldSubscriber, subscriber) {
        if (skaldMulticastIsMatch(msgName,
                    subscriber->regex, subscriber->pattern)) {
            // TODO: send to other skald
        }
    } // for each thread subscriber
}


static void skaldTrace(const SkalMsg* msg, const char* json)
{
    if (NULL == gTraceGroup) {
        return;
    }
    SKALASSERT(msg != NULL);
    char* allocatedJson = NULL;
    if (NULL == json) {
        allocatedJson = SkalMsgToJson(msg);
        json = allocatedJson;
    }

    const char* msgName = SkalMsgName(msg);
    CDSLIST_FOREACH(gTraceGroup->threadSubscribers,
            skaldThreadSubscriber, subscriber) {
        if (skaldMulticastIsMatch(msgName,
                    subscriber->regex, subscriber->pattern)) {
            (void)skaldSendOnSocket(subscriber->sockid, json);
        }
    }

    CDSLIST_FOREACH(gTraceGroup->skaldSubscribers,
            skaldSkaldSubscriber, subscriber) {
        if (skaldMulticastIsMatch(msgName,
                    subscriber->regex, subscriber->pattern)) {
            (void)skaldSendOnSocket(subscriber->sockid, json);
        }
    } // for each skald subscriber

    free(allocatedJson);
}
