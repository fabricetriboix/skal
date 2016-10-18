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

/** skald daemon
 *
 * The jobs of the skald daemon are the following:
 *  - Route messages between threads
 *  - Monitor which thread is blocked on what thread
 *  - Unblock blocked thread when the blocking thread crashed
 *  - Maintain a register of alarms
 *
 * Please note that references to "threads" are for threads in other processes.
 * The skald daemon does not run any application threads.
 */

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
    char       key[SKAL_NAME_MAX * 2]; /**< "alarm-origin:alarm-type" */
    SkalAlarm* alarm;
} skaldAlarm;


/** The different types of sockets */
typedef enum {
    /** Pipe server - to allow skald to terminate itself cleanly */
    SKALD_SOCKET_PIPE_SERVER,

    /** Pipe client - to tell skald to terminate itself */
    SKALD_SOCKET_PIPE_CLIENT,

    /** Peer - TODO */
    SKALD_SOCKET_PEER,

    /** Local server - for processes to connect to me */
    SKALD_SOCKET_LOCAL_SERVER,

    /** Local comm - one per process */
    SKALD_SOCKET_LOCAL_COMM
} skaldSocketType;


/** Structure that represents an item of `skaldThread.blocking`
 *
 * Because we know there is only ever going to be only one reference to this
 * structure, we don't keep track of the reference count.
 */
typedef struct {
    CdsMapItem item;
    char       name[SKAL_NAME_MAX];
} skaldBlocked;


/** Structure that holds information about a thread in our cluster
 *
 * This goes into the `gThreads`, `skaldProcess.threads` and
 * `skaldThread.blocked` maps.
 */
typedef struct {
    CdsMapItem item;

    /** Reference count */
    int ref;

    /** Name of the thread in question */
    char name[SKAL_NAME_MAX];

    /** Map of threads blocked by this thread; made of `skaldThread` */
    CdsMap* blocked;
} skaldThread;


/** Structure that holds information about a process under our management */
typedef struct {
    /** Name of the process in question */
    char name[SKAL_NAME_MAX];

    /** Map of threads in this process
     *
     * Items are of type `skaldThread`, the key is `skaldThread.name`.
     */
    CdsMap* threads;
} skaldProcess;


/** Structure that holds information related to a remote skald */
typedef struct {
    int TODO;
} skaldPeer;


/** Structure that holds information related to a socket */
typedef struct {
    skaldSocketType type;
    union {
        skaldPeer    peer;    /**< For `SKALD_SOCKET_PEER` */
        skaldProcess process; /**< For `SKALD_SOCKET_LOCAL_COMM` */
    };
} skaldSocketCtx;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Insert/remove an alarm from the alarm map
 *
 * The action (insert vs remove) depends on whether the `alarm` is on or off.
 *
 * The ownership of `alarm` is transferred to this function.
 *
 * @param alarm [in] Alarm to process; must not be NULL
 */
static void skaldAlarmProcess(SkalAlarm* alarm);


/** Unref an item in the alarm map */
static void skaldAlarmUnref(CdsMapItem* item);


/** Function to de-reference a skal-net socket context */
static void skaldSocketFree(void* context);


/** Create a new process following a connection request
 *
 * @param commSockid [in] Socket id of new process
 */
static void skaldHandleProcessConnection(int commSockid);


/** Handle the disconnection of a process
 *
 * A process would normally disconnect because it crashed. The main task of this
 * skald in this case is to wake up any thread from other processes blocked on a
 * thread of the crashed process.
 *
 * @param sockid  [in]     Id of the skal-net socket that disconnected
 * @param process [in,out] Socket structure of process that just disconnected
 */
static void skaldHandleProcessDisconnection(int sockid, skaldProcess* process);


/** Function to take a reference to a `skaldThread` item */
static void skaldThreadRef(skaldThread* thread);


/** Function to free a `skaldThread` */
static void skaldThreadUnref(CdsMapItem* item);


/** Helper function to unblock all threads blocked by the given thread
 *
 * This function sends `skal-xon` messages to all threads currently blocked by
 * the given `thread`.
 *
 * @param thread [in] Blocking thread; must not be NULL
 */
static void skaldThreadSendXon(const skaldThread* thread);


/** Send a message to the correct recipient
 *
 */
static void skaldMsgSend(SkalMsg* msg);


/** Process an incoming messge
 *
 * @param msg [in] Message to process
 */
static void skaldHandleMsg(const SkalMsg* msg);



/*------------------+
 | Global variables |
 +------------------*/


/** Domain managed by this skald */
static const char* gDomain = NULL;

/** Sockets
 *
 * There are 3 types of sockets:
 *  - A pipe that is used to terminate skald (eg: SIGTERM, etc.)
 *  - Sockets that link to a process on this machine
 *  - Sockets that link to another skald
 *
 * We use the skal-net ability to hold cookies for each socket to store
 * information related to each one of those sockets.
 */
static SkalNet* gNet = NULL;


/** Alarms currently active */
static CdsMap gAlarms = NULL;


/** Threads directly or indirectly connected to this skald */
static CdsMap gThreads = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkaldRun(const char* domain, const SkalNetAddr* localAddr,
        int npeers, const SkalNetAddr* peers, int pollTimeout_us)
{
    SKALASSERT(SkalIsAsciiString(domain, SKAL_NAME_MAX));
    SKALASSERT(localAddr != NULL);
    SKALASSERT(localAddr->unix.path[0] != '\0');
    SKALASSERT(SkalIsAsciiString(localAddr->unix.path,
                sizeof(localAddr->unix.path)));

    gDomain = domain;

    gNet = SkalNetCreate(pollTimeout_us, skaldSocketFree);

    gAlarms = CdsMapCreate( "alarms",           // name
                            0,                  // capacity
                            SkalStringCompare,  // compare
                            NULL,               // cookie
                            NULL,               // keyUnref
                            skaldAlarmUnref);   // itemUnref

    gThreads = CdsMapCreate("threads",          // name
                            0,                  // capacity
                            SkalStringCompare,  // compare
                            NULL,               // cookie
                            NULL,               // keyUnref
                            skaldThreadUnref);  // itemUnref

    // Create pipe to allow skald to terminate cleanly
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_SERVER;
    int sockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_PIPE, NULL, 0, ctx, 0);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(sockid == event->sockid);
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_PIPE_CLIENT;
    bool contextSet = SkalNetSetContext(gNet, event->conn.commSockid, ctx);
    SKALASSERT(contextSet);
    SkalNetEventUnref(event);

    // TODO: Connect to other skald's
    (void)peers;
    (void)npeers;

    // Create skald local socket
    ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_LOCAL_SERVER;
    (void)SkalNetServerCreate(gNet,
            SKAL_NET_TYPE_UNIX_SEQPACKET, &localAddr, 0, ctx, 0);

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

        case SKALD_SOCKET_PEER :
            SKALPANIC_MSG("Not yet implemented");
            break;

        case SKALD_SOCKET_LOCAL_SERVER :
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

        case SKALD_SOCKET_LOCAL_COMM :
            switch (event->type) {
            case SKAL_NET_EV_DISCONN :
                // This process is disconnecting from us
                skaldHandleProcessDisconnection(event->sockid, &ctx->process);
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
                                true, false,
                                "From process '%s'", ctx->process.name);
                        skaldAlarmProcess(alarm);
                    } else {
                        skaldHandleMsg(msg);
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
    CdsMapDestroy(gThreads);
    CdsMapDestroy(gAlarm);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skaldAlarmProcess(SkalAlarm* alarm)
{
    skaldAlarm* item = SkalMallocZ(sizeof(*item));
    const char* origin = SkalAlarmOrigin(alarm);
    if (NULL == origin) {
        origin = "";
    }
    snprintf(item->key, sizeof(item->key),
            "%s:%s", origin, SkalAlarmType(alarm));
    item->alarm = alarm;

    if (SkalAlarmIsOn(alarm)) {
        bool inserted = CdsMapInsert(gAlarms, item->key, &item->item);
        SKALASSERT(inserted);
    } else {
        (void)CdsMapRemove(gAlarms, item->key);
        skaldAlarmUnref(&item->item);
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


static void skaldSocketFree(void* context)
{
    skaldSocketCtx* ctx = (skaldSocketCtx*)context;

    switch (ctx->type) {
    case SKALD_SOCKET_LOCAL_COMM :
        CdsMapDestroy(ctx->peer.threads);
        break;
    default :
        break;
    }

    free(ctx);
}


static void skaldHandleProcessConnection(int commSockid)
{
    skaldSocketCtx* ctx = SkalMallocZ(sizeof(*ctx));
    ctx->type = SKALD_SOCKET_LOCAL_COMM;
    ctx->process.threads = CdsMapCreate(NULL,               // name
                                        0,                  // capacity
                                        SkalStringCompare,  // compare
                                        NULL,               // cookie
                                        NULL,               // keyUnref
                                        skaldThreadUnref);  // itemUnref
    bool ok = SkalNetSetContext(gNet, commSockid, ctx);
    SKALASSERT(ok);
}


static void skaldHandleProcessDisconnection(int sockid, skaldSocket* s)
{
    // TODO
}


static void skaldThreadRef(skaldThread* thread)
{
    SKALASSERT(thread != NULL);
    thread->ref++;
}


static void skaldThreadUnref(CdsMapItem* item)
{
    skaldThread* thread = (skaldThread*)item;
    SKALASSERT(thread != NULL);
    thread->ref--;
    if (thread->ref <= 0) {
        skaldThreadSendXon(thread);
        CdsMapDestroy(thread->blockers);
        free(thread);
    }
}


static void skaldThreadSendXon(const skaldThread* thread)
{
    SKALASSERT(thread != NULL);
    SKALASSERT(thread->blocking != NULL);
    CdsMapIteratorReset(thread->blocking, true);
    for (   CdsMapItem* item = CdsMapIteratorNext(thread->blocking, NULL);
            item != NULL;
            item = CdsMapIteratorNext(thread->blocking, NULL)) {
        skaldBlocked* blocked = (skaldBlocked*)item;
        SkalMsg* msg = SkalMsgCreate("skal-xon", blocked->name, 0, NULL);
        SkalMsgSetIFlags(msg, SKAL_MSG_IFLAG_INTERNAL);
        SkalMsgAddString(msg, "origin", thread->name);
        skaldMsgSend(msg);
    }
}


static void skaldHandleMsg(const SkalMsg* msg)
{
    // TODO
}


static void skaldMsgSend(SkalMsg* msg)
{
    // TODO
}
