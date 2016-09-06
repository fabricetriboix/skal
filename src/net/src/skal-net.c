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

#include "skal-net.h"
#include "cdsmap.h"
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <fcntl.h>
#include <string.h>
#include <unistd.h>
#include <errno.h>



/*----------------+
 | Types & Macros |
 +----------------*/


/** Item for the `skalNetSocket.cnxLessClients` map
 *
 * NB: We don't keep track of references, because these items are only stored in
 * the above map and never referenced elsewhere.
 */
typedef struct {
    CdsMapItem  item;
    SkalNetAddr address; // Address of client socket; also used as the key
    int         sockid;  // Index of client socket
} skalNetCnxLessClientItem;


/** Structure representing a single socket (or pipe end) */
typedef struct {
    int     fd;             // File descriptor
    int     fdRef;          // Reference counter to the file descriptor
    int     domain;         // As in the call to `socket(2)`, or -1 for pipes
    int     type;           // As in the call to `socket(2)`
    int     protocol;       // As in the call to `socket(2)`
    bool    isServer;       // Is it a server or a comm socket?
    bool    isFromServer;   // Comm socket: Is it born of a server socket?
    bool    isInProgress;   // Comm socket: Is a cnx in progress?
    bool    isCnxLess;      // This is a connection-less socket
    bool    ntfSend;        // Caller wants to send data on this socket
    int     bufsize_B;      // Server socket: Buffer size for comm sockets
    int64_t timeout_us;     // Cnx-less comm socket: Idle timeout
    int64_t lastActivity_us;// Cnx-less comm socket: Timestamp of last recv/send
    void*   context;        // Caller cookie
    CdsMap* cnxLessClients; // Cnx-less server socket: map of clients sockets,
                            //  items are of type `skalNetCnxLessClientItem`,
                            //  and keys are of type `SkalNetAddr`
    SkalNetAddr peer;       // Comm socket: Address of peer
} skalNetSocket;


struct SkalNet {
    int64_t        pollTimeout_us;
    int            nsockets;
    skalNetSocket* sockets;
    CdsList*       events; // List of `SkalNetEvent`
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Get a sensible buffer size from a caller-supplied value */
static int skalNetGetBufsize_B(int bufsize_B);


/** Compare two keys of the connection-less client map */
static int skalNetCnxLessClientKeyCmp(void* leftkey, void* rightkey,
        void* cookie);


/** De-reference a connection-less client item */
static void skalNetCnxLessClientUnref(CdsMapItem* item);


/** Convert a skal-net address to a POSIX address
 *
 * @param domain    [in]  Standard POSIX domain: `AF_UNIX`, `AF_INET`, etc.
 * @param skalAddr  [in]  skal-net address
 * @param posixAddr [out] POSIX address
 *
 * @return POSIX address length, in bytes
 */
static socklen_t skalNetAddrToPosix(int domain,
        const SkalNetAddr* skalAddr, struct sockaddr* posixAddr);


/** Convert a POSIX address to a skal-net address
 *
 * @param posixAddr [in]  POSIX address
 * @param skalAddr  [out] skal-net address
 *
 * @return The socket domain, like `AF_UNIX` or `AF_INET`
 */
static int skalNetPosixToAddr(const struct sockaddr* posixAddr,
        SkalNetAddr* skalAddr);


/** Allocate a new event structure
 *
 * The event structure will be reset to 0 and the reference count set to 1.
 *
 * @param type   [in] Event type
 * @param sockid [in] Id of socket that is generating the event
 *
 * @return The allocated event; this function never returns NULL
 */
static SkalNetEvent* skalNetEventAllocate(SkalNetEventType type,
        int sockid);


/** Allocate a new socket structure
 *
 * This function tries to re-use a free slot if any is available. If no free
 * slot is available, it expands the socket array.
 *
 * @param net [in,out] Where to allocate the new socket structure
 *
 * @return Id of the socket just created, which is its index in the socket array
 */
static int skalNetAllocateSocket(SkalNet* net);


/** Create a pipe
 *
 * This function will return the socket id for the reading side of the pipe. The
 * upper layer will be informed of the writing side of the pipe by a
 * `SKAL_NET_EV_CONN` event.
 *
 * @param net       [in,out] Socket set where to create the pipe; must not be
 *                           NULL
 * @param bufsize_B [in]     Size of pipe buffer, in bytes; if <=0, use default
 * @param context   [in]     Caller cookie
 *
 * @return Socket id of "server" end of the pipe (i.e. the reading side); this
 *         function never returns <0
 */
static int skalNetCreatePipe(SkalNet* net, int bufsize_B, void* context);


/** Create a server socket
 *
 * The combination (net, domain, type) must be a valid one for the underlying
 * OS.
 *
 * @param net       [in,out] Socket set where to create the new server
 *                           socket; must not be NULL
 * @param domain    [in]     Domain; must be either `AF_INET`, `AF_INET6` or
 *                           `AF_UNIX`
 * @param type      [in]     Type of socket to create; must be either
 *                           `SOCK_STREAM`, `SOCK_DGRAM` or `SOCK_SEQPACKET`
 * @param protocol  [in]     Protocol to use; 0 for default (which is TCP for
 *                           `SOCK_STREAM` or UDP for `SOCK_DGRAM`)
 * @param localAddr [in]     Address to listen to
 * @param bufsize_B [in]     Buffer size for comm sockets created out of this
 *                           server socket; <=0 for default
 * @param context   [in]     Caller cookie
 * @param extra     [in]     Timeout in us (for `SOCK_DGRAM`), otherwise backlog
 *
 * @return Id of the created socket; this function never returns <0
 */
static int skalNetCreateServer(SkalNet* net, int domain, int type,
        int protocol, const SkalNetAddr* localAddr,
        int bufsize_B, void* context, int extra);


/** Run `select(2)` on the sockets and enqueue the corresponding events
 */
static void skalNetSelect(SkalNet* net);


/** Handle a "read" event indicated by `select(2)` */
static void skalNetHandleIn(SkalNet* net, int sockid);


/** Handle a "write" event indicated by `select(2)` */
static void skalNetHandleOut(SkalNet* net, int sockid);


/** Handle an "exception" event indicated by `select(2)` */
static void skalNetHandleExcept(SkalNet* net, int sockid);


/** Handle a connection request from a client on a stream-based server socket */
static void skalNetAccept(SkalNet* net, int sockid);


/** Create a new comm socket from a server socket
 *
 * @param net    [in,out] Socket set where to create the new comm socket; must
 *                        not be NULL
 * @param sockid [in]     Id of server socket
 * @param fd     [in]     File descriptor of client socket; must be >=0
 * @param peer   [in]     Address of peer; may be NULL
 *
 * @return Id of new comm socket
 */
static int skalNetNewComm(SkalNet* net, int sockid, int fd,
        const SkalNetAddr* peer);


/** Receive a packet from a packet-based socket
 *
 * The `bufsize_B` field of the given socket is the maximum number of bytes that
 * will be read from the socket. If the actual packet was larger than that, it
 * will be silently truncated; the rest of the packet is lost.
 *
 * @param net    [in,out] Socket set where the socket to read from is; must not
 *                        be NULL
 * @param sockid [in]     Id of socket to read from
 * @param size_B [out]    Number of bytes read; must not be NULL
 * @param sender [out]    Address of sender; may be NULL
 *
 * @return Received data, allocated using `malloc(3)`; please call `free(3)` on
 *         it when finished with it
 */
static void* skalNetReadPacket(SkalNet* net, int sockid,
        int* size_B, SkalNetAddr* sender);


/** Send data over a packet-oriented socket
 *
 * A single send will be performed, and if all the data couldn't be sent at
 * once, this function will return `SKAL_NET_SEND_TRUNC`.
 *
 * @param net    [in,out] Socket set where the socket to send through is; must
 *                        not be NULL
 * @param sockid [in]     Id of socket to send through; must be a
 *                        packet-oriented socket
 * @param data   [in]     Data to send; must not be NULL
 * @param size_B [in]     Number of bytes to send; must be >0
 *
 * @return The send result
 */
static SkalNetSendResult skalNetSendPacket(SkalNet* net, int sockid,
        const void* data, int size_B);


/** Receive data from a stream-based socket
 *
 * The `bufsize_B` field of the given socket is the maximum number of bytes that
 * will be read from the socket. If there is more data in the socket OS buffer,
 * you will need to call `skalNetReadStream()` again.
 *
 * @param net    [in,out] Socket set where the socket to read from is; must not
 *                        be NULL
 * @param sockid [in]     Id of socket to read from
 * @param size_B [out]    Number of bytes read; must not be NULL
 *
 * @return Received data, allocated using `malloc(3)`; please call `free(3)` on
 *         it when finished with it
 */
static void* skalNetReadStream(SkalNet* net, int sockid, int* size_B);


/** Send data over a stream-oriented socket
 *
 * This function will make sure all the data is sent.
 *
 * @param net    [in,out] Socket set where the socket to send through is; must
 *                        not be NULL
 * @param sockid [in]     Id of socket to send from
 * @param data   [in]     Data to send; must not be NULL
 * @param size_B [in]     Number of bytes to send; must be >0
 *
 * @return The send result
 */
static SkalNetSendResult skalNetSendStream(SkalNet* net, int sockid,
        const void* data, int size_B);


/** Handle the reception of data on a connection-less server socket
 *
 * This function will create a new comm socket if the `sender` is unknown to
 * this server socket.
 *
 * *IMPORTANT* This function takes ownership of the `data`.
 *
 * @param net    [in,out] Socket set; must not be NULL
 * @param sockid [in]     Id of server socket; must point to a connection-less
 *                        server socket
 * @param sender [in]     Address of peer that sent this data
 * @param data   [in,out] Buffer containing the data; must not be NULL
 * @param size_B [in]     How many bytes have been received; must be >0
 */
static void skalNetHandleDataOnCnxLessServerSocket(SkalNet* net,
        int sockid, const SkalNetAddr* sender, void* data, int size_B);



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


bool SkalNetStringToIp4(const char* str, uint32_t* ip4)
{
    SKALASSERT(str != NULL);
    SKALASSERT(ip4 != NULL);

    struct in_addr inaddr;
    int ret = inet_aton(str, &inaddr);
    if (ret != 0) {
        *ip4 = ntohl(inaddr.s_addr);
    }
    return (ret != 0);
}


void SkalNetIp4ToString(uint32_t ip4, char* str, int capacity)
{
    SKALASSERT(str != NULL);
    SKALASSERT(capacity > 0);

    struct in_addr inaddr;
    inaddr.s_addr = htonl(ip4);
    char* tmp = inet_ntoa(inaddr);
    SKALASSERT(tmp != NULL);
    snprintf(str, capacity, "%s", tmp);
}


void SkalNetEventRef(SkalNetEvent* event)
{
    SKALASSERT(event != NULL);
    event->ref++;
}


void SkalNetEventUnref(SkalNetEvent* event)
{
    SKALASSERT(event != NULL);
    event->ref--;
    if (event->ref <= 0) {
        if (SKAL_NET_EV_IN == event->type) {
            free(event->in.data);
        }
        free(event);
    }
}


SkalNet* SkalNetCreate(int64_t pollTimeout_us)
{
    SkalNet* net = malloc(sizeof(*net));
    SKALASSERT(net != NULL);
    memset(net, 0, sizeof(*net));
    net->events = CdsListCreate(NULL, 0,
            (CdsListItemUnref)SkalNetEventUnref);
    if (pollTimeout_us > 0) {
        net->pollTimeout_us = pollTimeout_us;
    } else {
        net->pollTimeout_us = SKAL_NET_DEFAULT_POLL_TIMEOUT_us;
    }
    return net;
}


void SkalNetDestroy(SkalNet* net)
{
    SKALASSERT(net != NULL);
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        if (net->sockets[sockid].fd >= 0) {
            bool destroyed = SkalNetSocketDestroy(net, sockid);
            SKALASSERT(destroyed);
        }
    }
    CdsListDestroy(net->events);
    free(net->sockets);
    free(net);
}


int SkalNetServerCreate(SkalNet* net, SkalNetType sntype,
        const SkalNetAddr* localAddr, int bufsize_B, void* context, int extra)
{
    SKALASSERT(net != NULL);
    SKALASSERT((localAddr != NULL) || (SKAL_NET_TYPE_PIPE == sntype));

    int sockid = -1;
    switch (sntype) {
    case SKAL_NET_TYPE_PIPE :
        sockid = skalNetCreatePipe(net, bufsize_B, context);
        break;

    case SKAL_NET_TYPE_UNIX_STREAM :
        sockid = skalNetCreateServer(net, AF_UNIX, SOCK_STREAM, 0,
                localAddr, bufsize_B, context, extra);
        break;

    case SKAL_NET_TYPE_UNIX_DGRAM :
        sockid = skalNetCreateServer(net, AF_UNIX, SOCK_DGRAM, 0,
                localAddr, bufsize_B, context, extra);
        break;

    case SKAL_NET_TYPE_UNIX_SEQPACKET :
        sockid = skalNetCreateServer(net, AF_UNIX, SOCK_SEQPACKET, 0,
                localAddr, bufsize_B, context, extra);
        break;

    case SKAL_NET_TYPE_IP4_TCP :
        sockid = skalNetCreateServer(net, AF_INET, SOCK_STREAM, 0,
                localAddr, bufsize_B, context, extra);
        break;

    case SKAL_NET_TYPE_IP4_UDP :
        sockid = skalNetCreateServer(net, AF_INET, SOCK_DGRAM, 0,
                localAddr, bufsize_B, context, extra);
        break;

    default :
        SKALPANIC_MSG("Unsupported socket type: %d", (int)sntype);
        break;
    }
    return sockid;
}


int SkalNetCommCreate(SkalNet* net, SkalNetType sntype,
        const SkalNetAddr* localAddr, const SkalNetAddr* remoteAddr,
        int bufsize_B, void* context, int64_t timeout_us)
{
    SKALASSERT(net != NULL);
    SKALASSERT(remoteAddr != NULL);

    int domain;
    int type;
    int protocol = 0;
    switch (sntype) {
    case SKAL_NET_TYPE_PIPE :
        SKALPANIC_MSG("Can't create comm socket of type 'pipe'");
        break;

    case SKAL_NET_TYPE_UNIX_STREAM :
        domain = AF_UNIX;
        type = SOCK_STREAM;
        break;

    case SKAL_NET_TYPE_UNIX_DGRAM :
        domain = AF_UNIX;
        type = SOCK_DGRAM;
        break;

    case SKAL_NET_TYPE_UNIX_SEQPACKET :
        domain = AF_UNIX;
        type = SOCK_SEQPACKET;
        break;

    case SKAL_NET_TYPE_IP4_TCP :
        domain = AF_INET;
        type = SOCK_STREAM;
        break;

    case SKAL_NET_TYPE_IP4_UDP :
        domain = AF_INET;
        type = SOCK_DGRAM;
        break;

    default :
        SKALPANIC_MSG("Unknown type %d", (int)sntype);
        break;
    }

    // Allocate the socket & fill in the structure
    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* c = &(net->sockets[sockid]);
    c->domain = domain;
    c->type = type;
    c->protocol = protocol;
    c->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    if (SOCK_DGRAM == type) {
        c->isCnxLess = true;
        c->timeout_us = timeout_us;
    }
    c->peer = *remoteAddr;
    c->context = context;

    // Create the socket and enable address reuse.
    int fd = socket(domain, type, protocol);
    c->fd = fd;
    c->fdRef = 1;
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret != -1);

    // Make stream-oriented sockets non-blocking (so the calling thread is not
    // blocked while it is trying to connect).
    if (SOCK_STREAM == type) {
        int flags = fcntl(fd, F_GETFL);
        SKALASSERT(flags != -1);
        flags |= O_NONBLOCK;
        ret = fcntl(fd, F_SETFL, flags);
        SKALASSERT(0 == ret);
    }

    // Bind the socket if requested
    struct sockaddr sa;
    if ((localAddr != NULL) && (domain != AF_UNIX)) {
        // NB: It doesn't make sense to bind a UNIX client socket
        socklen_t len = skalNetAddrToPosix(domain, localAddr, &sa);
        ret = bind(fd, &sa, len);
        SKALASSERT(0 == ret);
    }

    // Set the socket buffer size
    ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
            &c->bufsize_B, sizeof(c->bufsize_B));
    SKALASSERT(0 == ret);
    ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
            &c->bufsize_B, sizeof(c->bufsize_B));
    SKALASSERT(0 == ret);

    // Initiate the connection (only for connection-oriented sockets)
    if (!(c->isCnxLess)) {
        socklen_t len = skalNetAddrToPosix(domain, remoteAddr, &sa);
        ret = connect(fd, &sa, len);
        if (ret < 0) {
            SKALASSERT(EINPROGRESS == errno);
            c->isInProgress = true;
        } else {
            // Connection has been established immediately; this would be
            // unusual (except maybe for `AF_UNIX` sockets), but we handle the
            // case nevertheless.
            SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_ESTABLISHED,
                    sockid);
            bool inserted = CdsListPushBack(net->events, &event->item);
            SKALASSERT(inserted);

            // Put the socket back in blocking mode
            if (SOCK_STREAM == type) {
                int flags = fcntl(fd, F_GETFL);
                SKALASSERT(flags != -1);
                flags &= ~O_NONBLOCK;
                ret = fcntl(fd, F_SETFL, flags);
                SKALASSERT(0 == ret);
            }
        }
    }

    return sockid;
}


SkalNetEvent* SkalNetPoll_BLOCKING(SkalNet* net)
{
    SKALASSERT(net != NULL);

    if (CdsListIsEmpty(net->events)) {
        skalNetSelect(net);
    }

    // Scan cnx-less comm sockets for timeouts
    int64_t now_us = SkalPlfNow_ns() / 1000LL;
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        skalNetSocket* s = &(net->sockets[sockid]);
        if ((s->fd >= 0) && !(s->isServer) && s->isCnxLess) {
            if ((now_us - s->lastActivity_us) > s->timeout_us) {
                SkalNetEvent* evdis = skalNetEventAllocate(SKAL_NET_EV_DISCONN,
                        sockid);
                bool inserted = CdsListPushBack(net->events, &evdis->item);
                SKALASSERT(inserted);
            }
        }
    }

    SkalNetEvent* event = (SkalNetEvent*)CdsListPopFront(net->events);
    if (event != NULL) {
        // We fill in the context at the last minute, in case the upper layer
        // changes (or sets) the context between the time the event is generated
        // and now.
        event->context = net->sockets[event->sockid].context;
    }
    return event;
}


bool SkalNetSetContext(SkalNet* net, int sockid, void* context)
{
    SKALASSERT(net != NULL);

    bool ok = false;
    if (    (sockid >= 0)
         && (sockid < net->nsockets)
         && (net->sockets[sockid].fd >= 0)) {
        net->sockets[sockid].context = context;
        ok = true;
    }
    return ok;
}


bool SkalNetWantToSend(SkalNet* net, int sockid, bool flag)
{
    SKALASSERT(net != NULL);

    bool ok = false;
    if (    (sockid >= 0)
         && (sockid < net->nsockets)
         && (net->sockets[sockid].fd >= 0)
         && (SOCK_STREAM == net->sockets[sockid].type)) {
        net->sockets[sockid].ntfSend = flag;
        ok = true;
    }
    return ok;
}


SkalNetSendResult SkalNetSend_BLOCKING(SkalNet* net, int sockid,
        const void* data, int size_B)
{
    SKALASSERT(net != NULL);

    SkalNetSendResult result = SKAL_NET_SEND_INVAL_SOCKID;
    if ((sockid >= 0) && (sockid < net->nsockets)) {
        skalNetSocket* c = &(net->sockets[sockid]);
        if ((c->fd >= 0) && !(c->isServer)) {
            if (SOCK_STREAM == c->type) {
                result = skalNetSendStream(net, sockid, data, size_B);
            } else {
                result = skalNetSendPacket(net, sockid, data, size_B);
            }
        }
    }
    return result;
}


bool SkalNetSocketDestroy(SkalNet* net, int sockid)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));

    bool destroyed = false;
    skalNetSocket* s = &(net->sockets[sockid]);
    if (s->fd >= 0) {
        s->fdRef--;
        if (s->fdRef <= 0) {
            int ret = -1;
            while (ret < 0) {
                ret = close(s->fd);
                if (ret < 0) {
                    SKALASSERT(EINTR == errno);
                }
            }
            s->fd = -1;
        }
        if (s->cnxLessClients != NULL) {
            CdsMapDestroy(s->cnxLessClients);
        }
        destroyed = true;
    }
    return destroyed;
}



/*-------------------------------------+
 | Implementation of private functions |
 +-------------------------------------*/


static int skalNetGetBufsize_B(int bufsize_B)
{
    if (bufsize_B <= 0) {
        bufsize_B = SKAL_NET_DEFAULT_BUFSIZE_B;
    }
    SKALASSERT(bufsize_B >= SKAL_NET_MIN_BUFSIZE_B);
    SKALASSERT(bufsize_B <= SKAL_NET_MAX_BUFSIZE_B);
    return bufsize_B;
}


static int skalNetCnxLessClientKeyCmp(void* leftkey, void* rightkey,
        void* cookie)
{
    return memcmp(leftkey, rightkey, sizeof(SkalNetAddr));
}


static void skalNetCnxLessClientUnref(CdsMapItem* item)
{
    free(item);
}


static socklen_t skalNetAddrToPosix(int domain,
        const SkalNetAddr* skalAddr, struct sockaddr* posixAddr)
{
    SKALASSERT(skalAddr != NULL);
    SKALASSERT(posixAddr != NULL);

    socklen_t len = sizeof(struct sockaddr);
    switch (domain) {
    case AF_UNIX :
        {
            struct sockaddr_un* sun = (struct sockaddr_un*)posixAddr;
            len = sizeof(*sun);
            sun->sun_family = AF_UNIX;
            snprintf(sun->sun_path, sizeof(sun->sun_path), "%s",
                    skalAddr->unix.path);
        }
        break;

    case AF_INET :
        {
            struct sockaddr_in* sin = (struct sockaddr_in*)posixAddr;
            len = sizeof(*sin);
            sin->sin_family = AF_INET;
            sin->sin_addr.s_addr = htonl(skalAddr->ip4.address);
            sin->sin_port = htons(skalAddr->ip4.port);
        }
        break;

    default :
        SKALPANIC_MSG("Unhandled domain %d", domain);
    }
    return len;
}


static int skalNetPosixToAddr(const struct sockaddr* posixAddr,
        SkalNetAddr* skalAddr)
{
    SKALASSERT(posixAddr != NULL);
    SKALASSERT(skalAddr != NULL);

    int domain = posixAddr->sa_family;
    switch (domain) {
    case AF_UNIX :
        {
            const struct sockaddr_un* sun =(const struct sockaddr_un*)posixAddr;
            snprintf(skalAddr->unix.path, sizeof(skalAddr->unix.path), "%s",
                    sun->sun_path);
        }
        break;

    case AF_INET :
        {
            const struct sockaddr_in* sin =(const struct sockaddr_in*)posixAddr;
            skalAddr->ip4.address = ntohl(sin->sin_addr.s_addr);
            skalAddr->ip4.port = ntohs(sin->sin_port);
        }
        break;

    default :
        SKALPANIC_MSG("Unhandled domain %d", domain);
    }
    return domain;
}


static SkalNetEvent* skalNetEventAllocate(SkalNetEventType type,
        int sockid)
{
    SkalNetEvent* event = malloc(sizeof(*event));
    SKALASSERT(event != NULL);

    // NB: Don't waste time resetting extra bytes
    memset(event, 0, sizeof(*event));
    event->ref = 1;
    event->type = type;
    event->sockid = sockid;
    return event;
}


static int skalNetAllocateSocket(SkalNet* net)
{
    SKALASSERT(net != NULL);

    // First, try to find an unused slot
    int sockid = -1;
    for (int i = 0; (i < net->nsockets) && (sockid < 0); i++) {
        if (net->sockets[i].fd < 0) {
            sockid = i;
        }
    }

    // If no slot is free, allocate a new one
    if (sockid < 0) {
        sockid = net->nsockets;
        (net->nsockets)++;
        net->sockets = realloc(net->sockets,
                net->nsockets * sizeof(*(net->sockets)));
        SKALASSERT(net->sockets != NULL);
    }

    skalNetSocket* s = &(net->sockets[sockid]);
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    return sockid;
}


static int skalNetCreatePipe(SkalNet* net, int bufsize_B, void* context)
{
    int sockid = skalNetAllocateSocket(net);
    int fds[2];
    int ret = pipe(fds);
    SKALASSERT(0 == ret);

    skalNetSocket* s = &(net->sockets[sockid]);
    s->fd = fds[0];
    s->fdRef = 1;
    s->domain = -1;
    s->type = SOCK_STREAM;
    s->protocol = 0;
    s->isServer = true;
    s->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    s->context = context;

    (void)skalNetNewComm(net, sockid, fds[1], NULL);

    // NB: The previous call might have moved the socket array, so be careful!
    s = &(net->sockets[sockid]);

    if (s->bufsize_B > 0) {
        int ret = fcntl(fds[1], F_SETPIPE_SZ, s->bufsize_B);
        SKALASSERT(ret >= 0);
    }
    return sockid;
}


static int skalNetCreateServer(SkalNet* net, int domain, int type,
        int protocol, const SkalNetAddr* localAddr,
        int bufsize_B, void* context, int extra)
{
    SKALASSERT(net != NULL);
    SKALASSERT(    (AF_INET == domain)
                || (AF_INET6 == domain)
                || (AF_UNIX == domain));
    SKALASSERT(    (SOCK_STREAM == type)
                || (SOCK_DGRAM == type)
                || (SOCK_SEQPACKET == type));
    SKALASSERT(localAddr != NULL);

    // Allocate the socket & fill in the structure
    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* s = &(net->sockets[sockid]);
    s->domain = domain;
    s->type = type;
    s->protocol = protocol;
    s->isServer = true;
    s->isCnxLess = (SOCK_DGRAM == type);
    s->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    if (s->isCnxLess) {
        if (extra > 0) {
            s->timeout_us = extra;
        } else {
            s->timeout_us = SKAL_NET_DEFAULT_TIMEOUT_us;
        }
    }
    s->context = context;
    if (s->isCnxLess) {
        s->cnxLessClients = CdsMapCreate(
                NULL,                       // name
                0,                          // capacity
                skalNetCnxLessClientKeyCmp, // compare
                NULL,                       // cookie
                NULL,                       // keyUnref
                skalNetCnxLessClientUnref); // itemUnref
    }

    // Create the socket & enable address reuse
    int fd = socket(domain, type, protocol);
    SKALASSERT(fd >= 0);
    s->fd = fd;
    s->fdRef = 1;
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret != -1);

    // Bind the socket
    struct sockaddr sa;
    socklen_t len = skalNetAddrToPosix(domain, localAddr, &sa);
    ret = bind(fd, &sa, len);
    SKALASSERT(0 == ret);

    if (s->isCnxLess) {
        // Set buffer size on connection-less server sockets, as they are used
        // to actually send and receive data.
        ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                &s->bufsize_B, sizeof(s->bufsize_B));
        SKALASSERT(0 == ret);
        ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                &s->bufsize_B, sizeof(s->bufsize_B));
        SKALASSERT(0 == ret);

    } else {
        // Place connection-based server socket in listening mode
        if (extra <= 0) {
            extra = SKAL_NET_DEFAULT_BACKLOG;
        }
        ret = listen(fd, extra);
        SKALASSERT(0 == ret);
    }

    return sockid;
}


static void skalNetSelect(SkalNet* net)
{
    int maxFd = -1;
    fd_set readfds;
    fd_set writefds;
    fd_set exceptfds;
    FD_ZERO(&readfds);
    FD_ZERO(&writefds);
    FD_ZERO(&exceptfds);

    bool hasWriteFds = false;
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        skalNetSocket* s = &(net->sockets[sockid]);
        if (s->fd >= 0) {
            FD_SET(s->fd, &readfds);
            FD_SET(s->fd, &exceptfds);
            if (s->ntfSend || s->isInProgress) {
                // NB: When `connect(2)` is called on a non-blocking socket
                // and if the connection can't be established immediately, a
                // "write" event will be generated by `poll(2)` when the
                // result of the connection operation is known.
                FD_SET(s->fd, &writefds);
                hasWriteFds = true;
            }
            if (s->fd > maxFd) {
                maxFd = s->fd;
            }
        }
    }

    struct timeval tv;
    tv.tv_sec  = net->pollTimeout_us / 1000000LL;
    tv.tv_usec = net->pollTimeout_us % 1000000LL;
    int count;
    if (maxFd < 0) {
        // No open socket in socket set
        count = select(0, NULL, NULL, NULL, &tv);
    } else {
        fd_set* pWriteFds = NULL;
        if (hasWriteFds) {
            pWriteFds = &writefds;
        }
        count = select(maxFd + 1, &readfds, pWriteFds, &exceptfds, &tv);
    }
    if (count < 0) {
        SKALASSERT(EINTR == errno);
        count = 0;
    }

    // NB: `net->sockets` may be reallocated in this loop, so we have to make
    // sure we always access it directly.
    for (int sockid = 0; (sockid < net->nsockets) && (count > 0); sockid++) {
        if (net->sockets[sockid].fd >= 0) {
            if (FD_ISSET(net->sockets[sockid].fd, &readfds)) {
                skalNetHandleIn(net, sockid);
            }
            if (FD_ISSET(net->sockets[sockid].fd, &writefds)) {
                skalNetHandleOut(net, sockid);
            }
            if (FD_ISSET(net->sockets[sockid].fd, &exceptfds)) {
                skalNetHandleExcept(net, sockid);
            }
            count--;
        }
    }
}


static void skalNetHandleIn(SkalNet* net, int sockid)
{
    skalNetSocket* s = &(net->sockets[sockid]);
    if (s->isServer) {
        if (s->isCnxLess) {
            // We received data on a connection-less server socket
            int size_B;
            SkalNetAddr sender;
            memset(&sender, 0, sizeof(sender));
            void* data = skalNetReadPacket(net, sockid, &size_B, &sender);
            if (data != NULL) {
                SKALASSERT(size_B > 0);
                skalNetHandleDataOnCnxLessServerSocket(net, sockid,
                        &sender, data, size_B);
            }
        } else if (s->domain >= 0) {
            // We received a connection request from a client
            skalNetAccept(net, sockid);
        } else {
            // This is a pipe
            int size_B;
            void* data = skalNetReadStream(net, sockid, &size_B);
            if (data != NULL) {
                SKALASSERT(size_B > 0);
                SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_IN,
                        sockid);
                event->in.size_B = size_B;
                event->in.data = data;
                bool inserted = CdsListPushBack(net->events, &event->item);
                SKALASSERT(inserted);
            }
        }
    } else {
        int size_B;
        void* data = NULL;
        if (SOCK_STREAM == s->type) {
            data = skalNetReadStream(net, sockid, &size_B);
        } else {
            data = skalNetReadPacket(net, sockid, &size_B, NULL);
        }
        if (data != NULL) {
            SKALASSERT(size_B > 0);
            SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_IN, sockid);
            event->in.size_B = size_B;
            event->in.data = data;
            bool inserted = CdsListPushBack(net->events, &event->item);
            SKALASSERT(inserted);
        }
    }
}


static void skalNetHandleOut(SkalNet* net, int sockid)
{
    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(!c->isServer);

    SkalNetEvent* event;
    if (c->isInProgress) {
        // A `connect(2)` operation has finished
        SKALASSERT(!c->isCnxLess);
        c->isInProgress = false;
        int err;
        socklen_t len = sizeof(err);
        int ret = getsockopt(c->fd, SOL_SOCKET, SO_ERROR, &err, &len);
        SKALASSERT(0 == ret);
        SKALASSERT(len == sizeof(err));

        if (0 == err) {
            // The `connect(2)` operation succeeded
            event = skalNetEventAllocate(SKAL_NET_EV_ESTABLISHED, sockid);
        } else {
            // The `connect(2)` operation did not succeed
            event = skalNetEventAllocate(SKAL_NET_EV_NOT_ESTABLISHED, sockid);
        }

    } else {
        SKALASSERT(SOCK_STREAM == c->type);
        event = skalNetEventAllocate(SKAL_NET_EV_OUT, sockid);
    }
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);
}


static void skalNetHandleExcept(SkalNet* net, int sockid)
{
    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_ERROR, sockid);
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);
}


static void skalNetAccept(SkalNet* net, int sockid)
{
    skalNetSocket* s = &(net->sockets[sockid]);
    struct sockaddr sa;
    socklen_t len = sizeof(sa);
    int fd = accept(s->fd, &sa, &len);
    if (fd < 0) {
        SKALASSERT(    (EINTR == errno)
                    || (ECONNABORTED == errno)
                    || (EPROTO == errno));
    } else {
        SkalNetAddr peer;
        int domain = skalNetPosixToAddr(&sa, &peer);
        SKALASSERT(domain == s->domain);

        (void)skalNetNewComm(net, sockid, fd, &peer);
    }
}


static int skalNetNewComm(SkalNet* net, int sockid, int fd,
        const SkalNetAddr* peer)
{
    SKALASSERT(fd >= 0);
    int commSockid = skalNetAllocateSocket(net);
    skalNetSocket* s = &(net->sockets[sockid]);
    skalNetSocket* c = &(net->sockets[commSockid]);
    c->fd = fd;
    c->fdRef = 1;
    c->domain = s->domain;
    c->type = s->type;
    c->protocol = s->protocol;
    c->isFromServer = true;
    c->isCnxLess = s->isCnxLess;
    c->bufsize_B = s->bufsize_B;
    c->timeout_us = s->timeout_us;
    if (peer != NULL) {
        c->peer = *peer;
    }

    if ((c->domain >= 0) && !(c->isCnxLess)) {
        // Set the socket buffer size
        // NB: If it's connection-less comm socket, the fd will be the same as
        // the server socket, so the buffer sizes have been set already.
        int ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                &c->bufsize_B, sizeof(c->bufsize_B));
        SKALASSERT(0 == ret);
        ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                &c->bufsize_B, sizeof(c->bufsize_B));
        SKALASSERT(0 == ret);
    }

    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_CONN, sockid);
    event->conn.commSockid = commSockid;
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);

    return commSockid;
}


static void* skalNetReadPacket(SkalNet* net, int sockid,
        int* size_B, SkalNetAddr* sender)
{
    SKALASSERT(net != NULL);
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < net->nsockets);
    SKALASSERT(size_B != NULL);
    SKALASSERT(sender != NULL);

    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(c->bufsize_B > 0);
    void* data = malloc(c->bufsize_B);
    SKALASSERT(data != NULL);

    int ret = -1;
    while (ret < 0) {
        struct sockaddr sa;
        socklen_t socklen;
        ret = recvfrom(c->fd, data, c->bufsize_B, 0, &sa, &socklen);
        if (ret < 0) {
            SKALASSERT(EINTR == errno);

        } else if (0 == ret) {
            // Peer has disconnected
            free(data);
            data = NULL;
            SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_DISCONN,
                    sockid);
            bool inserted = CdsListPushBack(net->events, &event->item);
            SKALASSERT(inserted);

        } else {
            if (sender != NULL) {
                int domain = skalNetPosixToAddr(&sa, sender);
                SKALASSERT(domain == c->domain);
            }
            if (c->isCnxLess) {
                c->lastActivity_us = SkalPlfNow_ns() / 1000LL;
            }
        }
    }

    *size_B = ret;
    return data;
}


static SkalNetSendResult skalNetSendPacket(SkalNet* net, int sockid,
        const void* data, int size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);

    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(!(c->isServer));
    SKALASSERT(c->type != SOCK_STREAM);

    struct sockaddr sa;
    socklen_t socklen = skalNetAddrToPosix(c->domain, &c->peer, &sa);
    SkalNetSendResult result = SKAL_NET_SEND_OK;
    bool done = false;
    while (!done) {
        int ret = sendto(c->fd, data, size_B, MSG_NOSIGNAL, &sa, socklen);
        if (ret < 0) {
            switch (errno) {
            case EINTR :
                // We have been interrupted by a signal => ignore and retry
                break;
            case EMSGSIZE :
                result = SKAL_NET_SEND_TOO_BIG;
                break;
            default :
                SKALPANIC_MSG("Unexpected errno while sending on a packet "
                        "socket: %s [%d]", strerror(errno), errno);
                break;
            }
        } else if (0 == ret) {
            SKALPANIC_MSG("Unexpected empty send");
        } else {
            done = true;
            if (ret < size_B) {
                result = SKAL_NET_SEND_TRUNC;
            }
            if (c->isCnxLess) {
                c->lastActivity_us = SkalPlfNow_ns() / 1000LL;
            }
        }
    }
    return result;
}


static void* skalNetReadStream(SkalNet* net, int sockid, int* size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));
    SKALASSERT(size_B != NULL);

    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(c->bufsize_B > 0);
    void* data = malloc(c->bufsize_B);
    SKALASSERT(data != NULL);

    bool done = false;
    int remaining_B = c->bufsize_B;
    int readSoFar_B = 0;
    while (!done && (remaining_B > 0)) {
        int ret;
        if (c->domain >= 0) {
            // This is a socket
            ret = recv(c->fd, data + readSoFar_B, remaining_B, MSG_DONTWAIT);
        } else {
            // This is a pipe
            ret = read(c->fd, data + readSoFar_B, remaining_B);
        }
        if (ret < 0) {
            if ((EAGAIN == errno) || (EWOULDBLOCK == errno)) {
                done = true;
            } else {
                SKALASSERT(EINTR == errno);
            }

        } else if (0 == ret) {
            done = true;
            if (readSoFar_B <= 0) {
                // Peer has disconnected
                free(data);
                data = NULL;
                SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_DISCONN,
                        sockid);
                bool inserted = CdsListPushBack(net->events, &event->item);
                SKALASSERT(inserted);
            }

        } else {
            readSoFar_B += ret;
            remaining_B -= ret;

            if (c->domain < 0) {
                // This is a pipe => Stop as soon as we read something
                done = true;
            }
        }
    }

    if (readSoFar_B <= 0) {
        free(data);
        data = NULL;
    }

    *size_B = readSoFar_B;
    return data;
}


static SkalNetSendResult skalNetSendStream(SkalNet* net, int sockid,
        const void* data, int size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < net->nsockets);
    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);

    SkalNetSendResult result = SKAL_NET_SEND_OK;
    while ((size_B > 0) && (SKAL_NET_SEND_OK == result)) {
        int ret;
        if (c->domain >= 0) {
            // This is a socket
            ret = send(c->fd, data, size_B, MSG_NOSIGNAL);
        } else {
            // This is a pipe
            ret = write(c->fd, data, size_B);
        }
        if (ret < 0) {
            switch (errno) {
            case EINTR :
                // We have been interrupted by a signal => ignore and retry
                break;
            case ECONNRESET :
                result = SKAL_NET_SEND_RESET;
                break;
            default :
                SKALPANIC_MSG("Unexpected errno while sending on a stream "
                        "socket: %s [%d]", strerror(errno), errno);
                break;
            }
        } else if (0 == ret) {
            // This should never happen since the socket is blocking...
            result = SKAL_NET_SEND_RESET;
        } else {
            size_B -= ret;
            data += ret;
        }
    }
    return result;
}


static void skalNetHandleDataOnCnxLessServerSocket(SkalNet* net,
        int sockid, const SkalNetAddr* sender, void* data, int size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));
    SKALASSERT(sender != NULL);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B > 0);

    skalNetSocket* s = &(net->sockets[sockid]);
    SKALASSERT(s->fd >= 0);
    SKALASSERT(s->isServer);
    SKALASSERT(s->isCnxLess);

    skalNetCnxLessClientItem* client = (skalNetCnxLessClientItem*)
        CdsMapSearch(s->cnxLessClients, (void*)sender);
    if (NULL == client) {
        // This is the first time we are receiving data from this client
        int commSockid = skalNetNewComm(net, sockid, s->fd, sender);
        net->sockets[commSockid].fdRef++; // `fd` is shared with server
        net->sockets[commSockid].lastActivity_us = SkalPlfNow_ns() / 1000LL;

        client = malloc(sizeof(*client));
        client->address = *sender;
        client->sockid = commSockid;
        bool inserted = CdsMapInsert(s->cnxLessClients,
                &client->address, &client->item);
        SKALASSERT(inserted);
    }

    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_IN, client->sockid);
    event->in.size_B = size_B;
    event->in.data = data;
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);
}
