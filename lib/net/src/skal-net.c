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

/** skal-net
 *
 * Whenever a generix POSIX address structure is required, we use a UNIX address
 * structure because it is the largest; neither `struct sockaddr` nor `struct
 * sockaddr_storage` are large enough to store a UNIX socket address. Unused
 * bytes must be set to 0.
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
#include <strings.h>
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
    CdsMapItem item;

    /** Address of client socket; also used as the key */
    struct sockaddr_un address;

    /** Index of client socket */
    int sockid;
} skalNetCnxLessClientItem;


/** Structure representing a single socket (or pipe end) */
typedef struct {
    /** File descriptor, or -1 if this structure is not used */
    int fd;

    /** Address family, as in the call to `socket(2)`, or -1 for pipes
     *
     * Examples: AF_INET, AF_UNIX, etc.
     */
    int domain;

    /** Socket type, as in the call to `socket(2)`
     *
     * Usually, one of: SOCK_STREAM, SOCK_DGRAM, SOCK_SEQPACKET
     */
    int type;

    /** Network protocol, as in the call to `socket(2)`
     *
     * Usually 0, but can be IPPROTO_SCTP.
     */
    int protocol;

    /** Is it a server or a comm socket?
     *
     * A server socket only receives connection requests and does not exchange
     * any data. There is one exception though: the "server" end of a pipe,
     * which can receive data (but not send any data).
     *
     * For a TCP socket, `listen(2)` would typically have been called on that
     * socket to make it into a server socket.
     */
    bool isServer;

    /** Comm socket only: Is this socket born of a server socket?
     *
     * A comm socket can either be created out of a server socket when it is
     * created from the server side, or is can be created directly when created
     * from the client side.
     *
     * For TCP socket, this would typically happen as a result of `accept(2)`.
     */
    bool isFromServer;

    /** Comm socket only: Is a connection in progress?
     *
     * When a connection is initiated from the client side to the server side,
     * and the connection could not be established immediately, this flag will
     * be set.
     *
     * This typically happens when `connect(2)` is called on a TCP socket.
     */
    bool cnxInProgress;

    /** Is this a connection-less socket?
     *
     * This flag would typically be set for UDP sockets.
     */
    bool isCnxLess;

    /** Upper layer wants to be notified that it can send data on this socket
     *
     * This flag is used to send large amounts of data as fast as possible
     * without blocking. This would result in a call to `select(2)` with the
     * relevant file descriptors set for the 'write' file descriptor set.
     */
    bool ntfSend;

    /** Buffer size for comm sockets, in bytes
     *
     * The kernel buffer socket size (or pipe buffer size for pipes) will be set
     * to this value.
     */
    int bufsize_B;

    /** Connection-less socket: Inactivity timeout, in us
     *
     * For connection-less comm socket, the socket will be closed if no data is
     * received or sent for this duration.
     *
     * For connection-less server sockets, this is the timeout that sockets born
     * out of this socket will inherit.
     */
    int64_t timeout_us;

    /** Connection-less comm socket: Timestamp of last received or sent data */
    int64_t lastActivity_us;

    /** Upper-layer cookie for this socket */
    void* context;

    /** Connection-less server socket: Map of clients sockets
     *
     * Items are of type `skalNetCnxLessClientItem`, and keys are of type
     * `struct sockaddr_un`.
     */
    CdsMap* cnxLessClients;

    /** Local address */
    struct sockaddr_un local;

    /** Comm socket: Peer address, in POSIX format */
    struct sockaddr_un peer;
} skalNetSocket;


/** Overall skal-net structure */
struct SkalNet {
    /** Polling timeout when calling `select(2)` */
    int64_t pollTimeout_us;

    /** Size of the `sockets` array */
    int nsockets;

    /** Array of socket structures
     *
     * Allocating and deallocating sockets is likely to be infrequent.
     * Consequently, we don't use a fancy data structure to hold them, but just
     * a simple, expandable array. This has the immense benefit of being
     * directly accessible using an index.
     */
    skalNetSocket* sockets;

    /** Queue of networking events */
    CdsList* events;

    /** Function to call to unreference socket contexts set by the upper layer*/
    SkalNetCtxUnref ctxUnref;
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
 * @param skalAddr  [in]  skal-net address; must not be NULL; must be of type
 *                        `SKAL_NET_TYPE_UNIX_*` or `SKAL_NET_TYPE_IP*`
 * @param posixAddr [out] POSIX address; must not be NULL
 *
 * @return POSIX address length, in bytes; always >0
 */
static socklen_t skalNetAddrToPosix(const SkalNetAddr* skalAddr,
        struct sockaddr_un* posixAddr);


/** Convert a POSIX address to a skal-net address
 *
 * @param posixAddr [in]  POSIX address
 * @param sockType  [in]  Socket type, like `SOCK_STREAM`, `SOCK_DGRAM`, etc.
 * @param skalAddr  [out] skal-net address
 */
static void skalNetPosixToAddr(const struct sockaddr_un* posixAddr,
        int sockType, SkalNetAddr* skalAddr);


/** Allocate a new event structure
 *
 * The event structure will be reset to 0 and the reference count set to 1.
 *
 * **IMPORTANT**: The event `context` is filled in when the event is popped out,
 * not in this function! Do not set the `context` after calling this function!
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
 * **WARNING** `net->sockets` might be moved by this function!
 *
 * @param net [in,out] Where to allocate the new socket structure
 *
 * @return Id of the socket just created, which is its index in the socket
 *         array; the socket structure will be invalidated
 */
static int skalNetAllocateSocket(SkalNet* net);


/** Create a pipe
 *
 * This function will return the socket id for the reading side of the pipe (the
 * "server" side). The upper layer will be informed of the writing side of the
 * pipe (the "client" side) by a `SKAL_NET_EV_CONN` event.
 *
 * @param net       [in,out] Socket set where to create the pipe; must not be
 *                           NULL
 * @param bufsize_B [in]     Size of pipe buffer, in bytes; if <=0, use default
 * @param context   [in]     Caller cookie
 *
 * @return Socket id of "server" end of the pipe (i.e. the reading side), or -1
 *         if system error
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
 * @param localAddr [in]     Address to listen to; must not be NULL
 * @param bufsize_B [in]     Buffer size for comm sockets created out of this
 *                           server socket, in bytes; <=0 for default
 * @param context   [in]     Upper layer cookie for this server socket
 * @param extra     [in]     If `type` is `SOCK_DGRAM`, this is the timeout in
 *                           us of the connection-less sockets that will be
 *                           created out of this server socket; for other types,
 *                           it is the backlog value, as used in `listen(2)`;
 *                           in any case, use 0 for the default value
 *
 * @return Id of the created socket; this function never returns <0
 */
static int skalNetCreateServer(SkalNet* net, int domain, int type, int protocol,
        const SkalNetAddr* localAddr, int bufsize_B, void* context, int extra);


/** Run `select(2)` on the sockets and enqueue the corresponding events
 *
 * Events will be enqueued in the `net->events` queue.
 *
 * @param net [in,out] Socket set to poll; must not be NULL
 */
static void skalNetSelect(SkalNet* net);


/** Handle a "read" event indicated by `select(2)` on socket `sockid` */
static void skalNetHandleIn(SkalNet* net, int sockid);


/** Handle a "write" event indicated by `select(2)` on socket `sockid` */
static void skalNetHandleOut(SkalNet* net, int sockid);


/** Handle an "exception" event indicated by `select(2)` on socket `sockid` */
static void skalNetHandleExcept(SkalNet* net, int sockid);


/** Handle a connection request from a client on a stream-based server socket */
static void skalNetAccept(SkalNet* net, int sockid);


/** Create a new comm socket from a server socket
 *
 * Call this function when the file descriptor for the client socket has been
 * created already.
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
        const struct sockaddr_un* peer);


/** Receive a packet from a packet-oriented socket
 *
 * The `bufsize_B` field of the given socket is the maximum number of bytes that
 * will be read from the socket. If the actual packet was larger than that, it
 * will be silently truncated and the rest of the packet will be lost.
 *
 * If `src` is not NULL, it will be filled in with the address returned by
 * `recvfrom(2)`.
 *
 * @param net    [in,out] Socket set where the socket to read from is; must not
 *                        be NULL
 * @param sockid [in]     Id of socket to read from
 * @param size_B [out]    Number of bytes read; must not be NULL
 * @param src    [out]    Address of packet source; may be NULL
 *
 * @return Received data, allocated using `malloc(3)`; please call `free(3)` on
 *         it when finished with it; returns NULL in case of error
 */
static void* skalNetReadPacket(SkalNet* net, int sockid,
        int* size_B, struct sockaddr_un* src);


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


/** Receive data from a stream-oriented socket
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
 *         it when finished with it; returns NULL in case of error
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
 * This function will create a new comm socket if the `src` is unknown to this
 * server socket.
 *
 * *IMPORTANT* This function takes ownership of the `data`.
 *
 * @param net    [in,out] Socket set; must not be NULL
 * @param sockid [in]     Id of server socket; must point to a connection-less
 *                        server socket
 * @param src    [in]     Address of source that sent this data; must not be
 *                        NULL
 * @param data   [in,out] Buffer containing the data; must not be NULL
 * @param size_B [in]     Size of above buffer, in bytes; must be >=0
 */
static void skalNetHandleDataOnCnxLessServerSocket(SkalNet* net,
        int sockid, const struct sockaddr_un* src, void* data, int size_B);



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
    char tmp[INET_ADDRSTRLEN];
    const char* ret = inet_ntop(AF_INET, &inaddr, tmp, sizeof(tmp));
    SKALASSERT(ret != NULL);
    snprintf(str, capacity, "%s", tmp);
}


bool SkalNetUrlToAddr(const char* url, SkalNetAddr* addr)
{
    SKALASSERT(url != NULL);
    SKALASSERT(addr != NULL);

    bool ok = true;
    bool isUnix = false;
    if (strncasecmp(url, "unix://", 7) == 0) {
        isUnix = true;
        addr->type = SKAL_NET_TYPE_UNIX_SEQPACKET;
        url += 7;

    } else if (strncasecmp(url, "unixs://", 8) == 0) {
        isUnix = true;
        addr->type = SKAL_NET_TYPE_UNIX_STREAM;
        url += 8;

    } else if (strncasecmp(url, "unixd://", 8) == 0) {
        isUnix = true;
        addr->type = SKAL_NET_TYPE_UNIX_DGRAM;
        url += 8;

    } else if (strncasecmp(url, "tcp://", 6) == 0) {
        addr->type = SKAL_NET_TYPE_IP4_TCP;
        url += 6;

    } else if (strncasecmp(url, "udp://", 6) == 0) {
        addr->type = SKAL_NET_TYPE_IP4_UDP;
        url += 6;

    } else {
        ok = false;
    }

    if (ok) {
        if (isUnix) {
            int n = snprintf(addr->unix.path, sizeof(addr->unix.path),
                    "%s", url + 7);
            if (n >= (int)sizeof(addr->unix.path)) {
                ok = false;
            }

        } else {
            ok = false;
            struct in_addr inaddr;
            int ret = inet_aton(url + 6, &inaddr);
            if (ret != 0) {
                addr->ip4.address = ntohl(inaddr.s_addr);
                const char* ptr = strchr(url + 6, ':');
                if (ptr != NULL) {
                    unsigned int tmp;
                    if ((sscanf(ptr + 1, "%u", &tmp) == 1) && (tmp <= 0xffff)) {
                        addr->ip4.port = tmp;
                        ok = true;
                    }
                }
            }
        }
    }

    return ok;
}


char* SkalNetAddrToUrl(const SkalNetAddr* addr)
{
    char* url = NULL;

    switch (addr->type) {
    case SKAL_NET_TYPE_UNIX_STREAM :
        url = SkalSPrintf("unixs://%s", addr->unix.path);
        break;
    case SKAL_NET_TYPE_UNIX_DGRAM :
        url = SkalSPrintf("unixd://%s", addr->unix.path);
        break;
    case SKAL_NET_TYPE_UNIX_SEQPACKET :
        url = SkalSPrintf("unix://%s", addr->unix.path);
        break;
    case SKAL_NET_TYPE_IP4_TCP :
    case SKAL_NET_TYPE_IP4_UDP :
        {
            const char* proto =
                (SKAL_NET_TYPE_IP4_TCP == addr->type) ? "tcp" : "udp";
            struct in_addr ina;
            ina.s_addr = htonl(addr->ip4.address);
            char tmp[INET_ADDRSTRLEN];
            const char* ret = inet_ntop(AF_INET, &ina, tmp, sizeof(tmp));
            SKALASSERT(NULL == ret);
            url = SkalSPrintf("%s://%s:%u",
                    proto, tmp, (unsigned int)addr->ip4.port);
        }
        break;
    default :
        SKALPANIC_MSG("Unknown address type: %d\n", (int)addr->type);
    }
    SKALASSERT(url != NULL);
    return url;
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


SkalNet* SkalNetCreate(int64_t pollTimeout_us, SkalNetCtxUnref ctxUnref)
{
    SkalNet* net = SkalMallocZ(sizeof(*net));
    net->events = CdsListCreate(NULL, 0, (CdsListItemUnref)SkalNetEventUnref);
    if (pollTimeout_us > 0) {
        net->pollTimeout_us = pollTimeout_us;
    } else {
        net->pollTimeout_us = SKAL_NET_DEFAULT_POLL_TIMEOUT_us;
    }
    net->ctxUnref = ctxUnref;
    return net;
}


void SkalNetDestroy(SkalNet* net)
{
    SKALASSERT(net != NULL);
    SKALASSERT((net->nsockets <= 0) || (net->sockets != NULL));
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        if (net->sockets[sockid].fd >= 0) {
            SkalNetSocketDestroy(net, sockid);
        }
    }
    CdsListDestroy(net->events);
    free(net->sockets);
    free(net);
}


int SkalNetServerCreate(SkalNet* net, const SkalNetAddr* localAddr,
        int bufsize_B, void* context, int extra)
{
    SKALASSERT(net != NULL);
    SKALASSERT(localAddr != NULL);

    int sockid = -1;
    switch (localAddr->type) {
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
        SKALPANIC_MSG("Unsupported socket type: %d", (int)localAddr->type);
        break;
    }
    return sockid;
}


int SkalNetCommCreate(SkalNet* net,
        const SkalNetAddr* localAddr, const SkalNetAddr* remoteAddr,
        int bufsize_B, void* context, int64_t timeout_us)
{
    SKALASSERT(net != NULL);
    SKALASSERT(remoteAddr != NULL);
    SKALASSERT(remoteAddr->type != SKAL_NET_TYPE_PIPE);
    if (localAddr != NULL) {
        SKALASSERT(localAddr->type == remoteAddr->type);
    }

    // Allocate the socket & fill in the structure
    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* c = &(net->sockets[sockid]);
    switch (remoteAddr->type) {
    case SKAL_NET_TYPE_UNIX_STREAM :
        c->domain = AF_UNIX;
        c->type = SOCK_STREAM;
        break;

    case SKAL_NET_TYPE_UNIX_DGRAM :
        c->domain = AF_UNIX;
        c->type = SOCK_DGRAM;
        break;

    case SKAL_NET_TYPE_UNIX_SEQPACKET :
        c->domain = AF_UNIX;
        c->type = SOCK_SEQPACKET;
        break;

    case SKAL_NET_TYPE_IP4_TCP :
        c->domain = AF_INET;
        c->type = SOCK_STREAM;
        break;

    case SKAL_NET_TYPE_IP4_UDP :
        c->domain = AF_INET;
        c->type = SOCK_DGRAM;
        break;

    default :
        SKALPANIC_MSG("Unknown type %d", (int)remoteAddr->type);
        break;
    }
    c->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    if (SOCK_DGRAM == c->type) {
        c->isCnxLess = true;
        if (timeout_us > 0) {
            c->timeout_us = timeout_us;
        } else {
            c->timeout_us = SKAL_NET_DEFAULT_TIMEOUT_us;
        }
        c->lastActivity_us = SkalPlfNow_us();
    }
    c->context = context;
    (void)skalNetAddrToPosix(remoteAddr, &c->peer);

    // Create the socket
    int fd = socket(c->domain, c->type, c->protocol);
    if (fd < 0) {
        char* url = SkalNetAddrToUrl(remoteAddr);
        SkalLog("socket(domain=%d, type=%d, protocol=%d) failed: errno=%d [%s] [remote=%s]",
                c->domain, c->type, c->protocol, errno, strerror(errno), url);
        free(url);
        return -1;
    }

    // Enable address reuse
    c->fd = fd;
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret >= 0);

    // Make stream-oriented sockets non-blocking (so the calling thread is not
    // blocked while it is trying to connect)
    if (SOCK_STREAM == c->type) {
        int flags = fcntl(fd, F_GETFL);
        SKALASSERT(flags != -1);
        flags |= O_NONBLOCK;
        ret = fcntl(fd, F_SETFL, flags);
        SKALASSERT(0 == ret);
    }

    // Bind the socket if requested; NB: We always bind UNIX sockets
    struct sockaddr_un sun;
    socklen_t len;
    if ((localAddr != NULL) || (AF_UNIX == c->domain)) {
        if (AF_UNIX == c->domain) {
            // Generate a unique, random, path for the comm socket to bind to
            // NB: We require this so that each connnection-less UNIX comm
            // socket can be distinguished by the server socket in `recvfrom()`.
            snprintf(   sun.sun_path, sizeof(sun.sun_path),
                        "%s%cskal-%d-%016llx-%08lx.sock",
                        SkalPlfTmpDir(),
                        SkalPlfDirSep(),
                        SkalPlfTid(),
                        (unsigned long long)SkalPlfNow_ns(),
                        (unsigned long)SkalPlfRandomU32());
            sun.sun_family = AF_UNIX;
            len = sizeof(sun);
        } else {
            len = skalNetAddrToPosix(localAddr, &sun);
        }
        ret = bind(fd, (const struct sockaddr*)&sun, len);
        if (ret < 0) {
            SkalNetAddr addr;
            skalNetPosixToAddr(&sun, c->type, &addr);
            char* local = SkalNetAddrToUrl(&addr); // `localAddr` may be NULL
            char* remote = SkalNetAddrToUrl(remoteAddr);
            SkalLog("bind(%s) failed: errno=%d [%s] [remote=%s]",
                    local, errno, strerror(errno), remote);
            free(local);
            free(remote);
            SkalNetSocketDestroy(net, sockid);
            return -1;
        }
    }

    // Set the socket buffer size
    ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
            &c->bufsize_B, sizeof(c->bufsize_B));
    SKALASSERT(0 == ret);
    ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
            &c->bufsize_B, sizeof(c->bufsize_B));
    SKALASSERT(0 == ret);

    // Connect to peer
    len = skalNetAddrToPosix(remoteAddr, &sun);
    ret = connect(fd, (const struct sockaddr*)&sun, len);
    if (ret < 0) {
        switch (errno) {
        case ECONNREFUSED :
            {
                // We might get an immediate refusal in the case of UNIX
                // sockets, when the socket file exists but no server is
                // listening at the other end.
                SkalNetEvent* event = skalNetEventAllocate(
                        SKAL_NET_EV_NOT_ESTABLISHED, sockid);
                bool inserted = CdsListPushBack(net->events, &event->item);
                SKALASSERT(inserted);
            }
            break;
        case EINPROGRESS :
            c->cnxInProgress = true;
            break;
        default :
            {
                // Something unexpected happened...
                char* remote = SkalNetAddrToUrl(remoteAddr);
                SkalLog("connect(%s) failed: errno=%d [%s]",
                        remote, errno, strerror(errno));
                free(remote);
                SkalNetSocketDestroy(net, sockid);
                return -1;
            }
            break;
        }
    } else {
        // Connection has been established immediately; this would be
        // unusual, except for UNIX sockets.
        SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_ESTABLISHED,
                sockid);
        bool inserted = CdsListPushBack(net->events, &event->item);
        SKALASSERT(inserted);

        // Put stream sockets back in blocking mode
        if (SOCK_STREAM == c->type) {
            int flags = fcntl(fd, F_GETFL);
            SKALASSERT(flags != -1);
            flags &= ~O_NONBLOCK;
            ret = fcntl(fd, F_SETFL, flags);
            SKALASSERT(0 == ret);
        }
    }

    // Get local address
    len = sizeof(c->local);
    ret = getsockname(fd, (struct sockaddr*)&c->local, &len);
    SKALASSERT(0 == ret);
    return sockid;
}


SkalNetEvent* SkalNetPoll_BLOCKING(SkalNet* net)
{
    SKALASSERT(net != NULL);

    if (CdsListIsEmpty(net->events)) {
        skalNetSelect(net);
    }

    // Scan cnx-less comm sockets for timeouts
    int64_t now_us = SkalPlfNow_us();
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        skalNetSocket* s = &(net->sockets[sockid]);
        if ((s->fd >= 0) && !(s->isServer) && s->isCnxLess) {
            if ((now_us - s->lastActivity_us) > s->timeout_us) {
                SkalNetEvent* evdisconn = skalNetEventAllocate(
                        SKAL_NET_EV_DISCONN, sockid);
                bool inserted = CdsListPushBack(net->events, &evdisconn->item);
                SKALASSERT(inserted);
            }
        }
    }

    SkalNetEvent* event = (SkalNetEvent*)CdsListPopFront(net->events);
    if (event != NULL) {
        if (net->sockets[event->sockid].fd < 0) {
            // Socket has been closed by upper layer after the event has been
            // generated.
            //  => Drop the event
            SkalNetEventUnref(event);
            event = NULL;
        } else {
            // We fill in the context at the last minute, in case the upper
            // layer changes (or sets) it between the time the event is
            // generated and now.
            event->context = net->sockets[event->sockid].context;
        }
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


void* SkalNetGetContext(const SkalNet* net, int sockid)
{
    SKALASSERT(net != NULL);

    void* context = NULL;
    if (    (sockid >= 0)
         && (sockid < net->nsockets)
         && (net->sockets[sockid].fd >= 0)) {
        context = net->sockets[sockid].context;
    }
    return context;
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


void SkalNetSocketDestroy(SkalNet* net, int sockid)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));

    skalNetSocket* s = &(net->sockets[sockid]);
    if (s->fd >= 0) {
        // For connection-less server sockets, the fd is shared between the
        // server and all the clients created out of this server.
        bool canClose = true;
        if (s->isCnxLess) {
            for (int i = 0; (i < net->nsockets) && canClose; i++) {
                if ((net->sockets[i].fd == s->fd) && (sockid != i)) {
                    canClose = false;
                }
            }
        }
        if (canClose) {
            // NB: Causes and handling of failures of `close()` are quite
            // ambiguous and ill-defined... So we just call it and hope for the
            // best.
            (void)shutdown(s->fd, SHUT_RDWR);
            (void)close(s->fd);
        }

        if ((net->ctxUnref != NULL) && (s->context != NULL)) {
            net->ctxUnref(s->context);
        }

        if (s->cnxLessClients != NULL) {
            CdsMapDestroy(s->cnxLessClients);
        }
        if ((AF_UNIX == s->domain) && !(s->isFromServer)) {
            unlink(s->local.sun_path);
        }
        s->fd = -1;
    }
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


static socklen_t skalNetAddrToPosix(const SkalNetAddr* skalAddr,
        struct sockaddr_un* posixAddr)
{
    SKALASSERT(skalAddr != NULL);
    SKALASSERT(posixAddr != NULL);

    socklen_t len;
    switch (skalAddr->type) {
    case SKAL_NET_TYPE_UNIX_STREAM :
    case SKAL_NET_TYPE_UNIX_DGRAM :
    case SKAL_NET_TYPE_UNIX_SEQPACKET :
        {
            len = sizeof(*posixAddr);
            posixAddr->sun_family = AF_UNIX;
            int n = snprintf(posixAddr->sun_path, sizeof(posixAddr->sun_path),
                    "%s", skalAddr->unix.path);
            SKALASSERT(n < (int)sizeof(posixAddr->sun_path));
        }
        break;

    case SKAL_NET_TYPE_IP4_TCP :
    case SKAL_NET_TYPE_IP4_UDP :
        {
            struct sockaddr_in* sin = (struct sockaddr_in*)posixAddr;
            len = sizeof(*sin);
            sin->sin_family = AF_INET;
            sin->sin_addr.s_addr = htonl(skalAddr->ip4.address);
            sin->sin_port = htons(skalAddr->ip4.port);
        }
        break;

    default :
        SKALPANIC_MSG("Unhandled socket type %d", (int)skalAddr->type);
    }
    SKALASSERT(len > 0);
    return len;
}


static void skalNetPosixToAddr(const struct sockaddr_un* posixAddr,
        int sockType, SkalNetAddr* skalAddr)
{
    SKALASSERT(posixAddr != NULL);
    SKALASSERT(skalAddr != NULL);

    switch (posixAddr->sun_family) {
    case AF_UNIX :
        {
            snprintf(skalAddr->unix.path, sizeof(skalAddr->unix.path), "%s",
                    posixAddr->sun_path);
            switch (sockType) {
            case SOCK_STREAM:
                skalAddr->type = SKAL_NET_TYPE_UNIX_STREAM;
                break;
            case SOCK_DGRAM :
                skalAddr->type = SKAL_NET_TYPE_UNIX_DGRAM;
                break;
            case SOCK_SEQPACKET :
                skalAddr->type = SKAL_NET_TYPE_UNIX_SEQPACKET;
                break;
            default :
                SKALPANIC_MSG("Unhandled socket type %d", sockType);
            }
        }
        break;

    case AF_INET :
        {
            const struct sockaddr_in* sin =(const struct sockaddr_in*)posixAddr;
            skalAddr->ip4.address = ntohl(sin->sin_addr.s_addr);
            skalAddr->ip4.port = ntohs(sin->sin_port);
            switch (sockType) {
            case SOCK_STREAM :
                skalAddr->type = SKAL_NET_TYPE_IP4_TCP;
                break;
            case SOCK_DGRAM :
                skalAddr->type = SKAL_NET_TYPE_IP4_UDP;
                break;
            default :
                SKALPANIC_MSG("Unhandled socket type %d", sockType);
            }
        }
        break;

    default :
        SKALPANIC_MSG("Unhandled domain %d", posixAddr->sun_family);
    }
}


static SkalNetEvent* skalNetEventAllocate(SkalNetEventType type,
        int sockid)
{
    SkalNetEvent* event = SkalMallocZ(sizeof(*event));
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
        net->sockets = SkalRealloc(net->sockets,
                net->nsockets * sizeof(*(net->sockets)));
    }

    skalNetSocket* s = &(net->sockets[sockid]);
    memset(s, 0, sizeof(*s));
    s->fd = -1;
    return sockid;
}


static int skalNetCreatePipe(SkalNet* net, int bufsize_B, void* context)
{
    int fds[2];
    int ret = pipe(fds);
    if (ret < 0) {
        SkalLog("pipe() failed: errno=%d [%s]", errno, strerror(errno));
        return -1;
    }

    // Make the reading end of the pipe non-blocking
    int flags = fcntl(fds[0], F_GETFL);
    SKALASSERT(flags != -1);
    flags |= O_NONBLOCK;
    ret = fcntl(fds[0], F_SETFL, flags);
    SKALASSERT(0 == ret);

    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* s = &(net->sockets[sockid]);
    s->fd = fds[0];
    s->domain = -1;
    s->type = SOCK_STREAM;
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


static int skalNetCreateServer(SkalNet* net, int domain, int type, int protocol,
        const SkalNetAddr* localAddr, int bufsize_B, void* context, int extra)
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
    s->context = context;
    if (s->isCnxLess) {
        if (extra > 0) {
            s->timeout_us = extra;
        } else {
            s->timeout_us = SKAL_NET_DEFAULT_TIMEOUT_us;
        }
        s->cnxLessClients = CdsMapCreate(
                NULL,                       // name
                0,                          // capacity
                skalNetCnxLessClientKeyCmp, // compare
                NULL,                       // cookie
                NULL,                       // keyUnref
                skalNetCnxLessClientUnref); // itemUnref
    }

    // Create the BSD socket
    int fd = socket(domain, type, protocol);
    if (fd < 0) {
        char* local = SkalNetAddrToUrl(localAddr);
        SkalLog("socket(domain=%d, type=%d, protocol=%d) failed: errno=%d [%s] [localAddr=%s]",
                domain, type, protocol, errno, strerror(errno), local);
        free(local);
        CdsMapDestroy(s->cnxLessClients);
        s->cnxLessClients = NULL;
        return -1;
    }
    s->fd = fd;

    // Enable address reuse
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret != -1);

    // Bind the socket
    socklen_t len = skalNetAddrToPosix(localAddr, &s->local);
    ret = bind(fd, (const struct sockaddr*)(&s->local), len);
    if (ret < 0) {
        char* local = SkalNetAddrToUrl(localAddr);
        SkalLog("bind(%s) failed: errno=%d [%s]",
                local, errno, strerror(errno));
        free(local);
        SkalNetSocketDestroy(net, sockid);
        return -1;
    }

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
        // Place connection-based server sockets in listening mode
        int backlog = SKAL_NET_DEFAULT_BACKLOG;
        if (extra > 0) {
            backlog = extra;
        }
        ret = listen(fd, backlog);
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

    // Fill in fd sets
    bool hasWriteFds = false;
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        skalNetSocket* s = &(net->sockets[sockid]);
        if (s->fd >= 0) {
            FD_SET(s->fd, &readfds);
            FD_SET(s->fd, &exceptfds);
            if (s->ntfSend || s->cnxInProgress) {
                // NB: When `connect(2)` is called on a non-blocking socket
                // and if the connection can't be established immediately, a
                // "write" event will be generated by `select(2)` when the
                // result of the connection operation is known.
                FD_SET(s->fd, &writefds);
                hasWriteFds = true;
            }
            if (s->fd > maxFd) {
                maxFd = s->fd;
            }
        }
    }

    // Call `select(2)`
    fd_set* pWriteFds = NULL;
    if (hasWriteFds) {
        pWriteFds = &writefds;
    }
    struct timeval tv;
    tv.tv_sec  = net->pollTimeout_us / 1000000LL;
    tv.tv_usec = net->pollTimeout_us % 1000000LL;
    int count = select(maxFd + 1, &readfds, pWriteFds, &exceptfds, &tv);
    if (count < 0) {
        SKALASSERT(EINTR == errno);
        count = 0;
    }

    // Analyse results from `select(2)` call
    // NB: `net->sockets` may be reallocated in this loop, so we have to make
    // sure we always access it directly.
    for (int sockid = 0; (sockid < net->nsockets) && (count > 0); sockid++) {
        if (net->sockets[sockid].fd >= 0) {
            bool marked = false;
            if (FD_ISSET(net->sockets[sockid].fd, &readfds)) {
                skalNetHandleIn(net, sockid);
                marked = true;
            }
            if (hasWriteFds && FD_ISSET(net->sockets[sockid].fd, &writefds)) {
                skalNetHandleOut(net, sockid);
                marked = true;
            }
            if (FD_ISSET(net->sockets[sockid].fd, &exceptfds)) {
                skalNetHandleExcept(net, sockid);
                marked = true;
            }
            if (marked) {
                count--;
            }
        } // if this socket is open
    } // for each socket
}


static void skalNetHandleIn(SkalNet* net, int sockid)
{
    skalNetSocket* s = &(net->sockets[sockid]);
    if (s->isServer) {
        if (s->isCnxLess) {
            // We received data on a connection-less server socket
            // NB: A connection-less socket is necessarily packet-based
            int size_B;
            struct sockaddr_un src;
            memset(&src, 0, sizeof(src));
            void* data = skalNetReadPacket(net, sockid, &size_B, &src);
            if (data != NULL) {
                SKALASSERT(size_B > 0);
                skalNetHandleDataOnCnxLessServerSocket(net, sockid,
                        &src, data, size_B);
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

    SkalNetEvent* event = NULL;
    if (c->cnxInProgress) {
        // A `connect(2)` operation has finished
        SKALASSERT(!c->isCnxLess);
        c->cnxInProgress = false;
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
    SKALASSERT(event != NULL);
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

    struct sockaddr_un sun;
    socklen_t len = sizeof(sun);
    int fd = accept(s->fd, (struct sockaddr*)&sun, &len);
    if (fd < 0) {
        SkalLog("accept() failed: errno=%d [%s]", errno, strerror(errno));
    } else {
        (void)skalNetNewComm(net, sockid, fd, &sun);
    }
}


static int skalNetNewComm(SkalNet* net, int sockid, int fd,
        const struct sockaddr_un* peer)
{
    SKALASSERT(fd >= 0);
    int commSockid = skalNetAllocateSocket(net);
    skalNetSocket* s = &(net->sockets[sockid]);
    skalNetSocket* c = &(net->sockets[commSockid]);
    c->fd = fd;
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

    if (c->domain >= 0) {
        socklen_t len = sizeof(c->local);
        int ret = getsockname(c->fd, (struct sockaddr*)&c->local, &len);
        SKALASSERT(0 == ret);

        if (!(c->isCnxLess)) {
            // Set the socket buffer size
            // NB: If it's connection-less comm socket, the fd will be the same
            // as the server socket, so the buffer sizes have been set already.
            int ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                    &c->bufsize_B, sizeof(c->bufsize_B));
            SKALASSERT(0 == ret);
            ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                    &c->bufsize_B, sizeof(c->bufsize_B));
            SKALASSERT(0 == ret);
        }
    }

    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_CONN, sockid);
    event->conn.commSockid = commSockid;
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);

    return commSockid;
}


static void* skalNetReadPacket(SkalNet* net, int sockid,
        int* size_B, struct sockaddr_un* src)
{
    SKALASSERT(net != NULL);
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < net->nsockets);
    SKALASSERT(size_B != NULL);

    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(c->bufsize_B > 0);
    void* data = SkalMalloc(c->bufsize_B);

    socklen_t socklen = sizeof(*src);
    int ret = recvfrom(c->fd, data, c->bufsize_B, 0, src, &socklen);

    if ((0 == ret) || ((ret < 0) && (ECONNRESET == errno))) {
        // NB: Various domains (UNIX, IPv4, etc.) allow empty datagrams, but we
        // assume no empty datagram is ever sent to us. We instead assume an
        // empty read means the connection has been closed (for example for
        // SOCK_SEQPACKET sockets.
        free(data);
        data = NULL;
        ret = 0;
        SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_DISCONN, sockid);
        bool inserted = CdsListPushBack(net->events, &event->item);
        SKALASSERT(inserted);

    } else if (ret < 0) {
        free(data);
        data = NULL;
        ret = 0;
        SkalNetAddr addr;
        skalNetPosixToAddr(&c->local, c->type, &addr);
        char* url = SkalNetAddrToUrl(&addr);
        SkalLog("recvfrom() failed: errno=%d [%s] [local=%s]",
                errno, strerror(errno), url);
        free(url);
        SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_ERROR, sockid);
        bool inserted = CdsListPushBack(net->events, &event->item);
        SKALASSERT(inserted);
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

    socklen_t socklen;
    switch (c->domain) {
    case AF_INET:
        socklen = sizeof(struct sockaddr_in);
        break;
    case AF_UNIX :
        socklen = sizeof(struct sockaddr_un);
        break;
    default :
        SKALPANIC_MSG("Unhandled socket domain: %d", c->domain);
        break;
    }

    SkalNetSendResult result = SKAL_NET_SEND_OK;
    bool done = false;
    while (!done) {
        int ret = sendto(c->fd, data, size_B, MSG_NOSIGNAL,
                (const struct sockaddr*)&c->peer, socklen);
        if (ret < 0) {
            switch (errno) {
            case EINTR :
                // We have been interrupted by a signal => ignore and retry
                break;
            case EMSGSIZE :
                result = SKAL_NET_SEND_TOO_BIG;
                done = true;
                break;
            default :
                {
                    // Unexpected error
                    SkalNetAddr addr;
                    skalNetPosixToAddr(&c->peer, c->type, &addr);
                    char* url = SkalNetAddrToUrl(&addr);
                    SkalLog("Unexpected errno while sending on a packet socket to %s: %s [%d]",
                            url, strerror(errno), errno);
                    free(url);
                    result = SKAL_NET_SEND_ERROR;
                    done = true;
                }
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
                c->lastActivity_us = SkalPlfNow_us();
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
    void* data = SkalMalloc(c->bufsize_B);

    bool done = false;
    int remaining_B = c->bufsize_B;
    int readSoFar_B = 0;
    while (!done && (remaining_B > 0)) {
        int ret;
        if (c->domain >= 0) {
            // This is a socket
            // Please note the `MSG_DONTWAIT` flag, which enables non-blocking
            // operation for this `recv(2)` call only
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
            // End-of-file
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

#if 0
            if (c->domain < 0) {
                // This is a pipe => Stop as soon as we read something
                // TODO why?
                done = true;
            }
#endif
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
                SKALPANIC_MSG("Unexpected errno while sending on a stream socket: %s [%d]",
                        strerror(errno), errno);
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
        int sockid, const struct sockaddr_un* src, void* data, int size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));
    SKALASSERT(src != NULL);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B >= 0);

    skalNetSocket* s = &(net->sockets[sockid]);
    SKALASSERT(s->fd >= 0);
    SKALASSERT(s->isServer);
    SKALASSERT(s->isCnxLess);

    skalNetCnxLessClientItem* client = (skalNetCnxLessClientItem*)
        CdsMapSearch(s->cnxLessClients, (void*)src);
    if (NULL == client) {
        // This is the first time we are receiving data from this client
        int commSockid = skalNetNewComm(net, sockid, s->fd, src);
        // NB: Careful! The previous call might have re-allocated `net->sockets`

        client = SkalMallocZ(sizeof(*client));
        client->address = *src;
        client->sockid = commSockid;
        bool inserted = CdsMapInsert(net->sockets[sockid].cnxLessClients,
                &client->address, &client->item);
        SKALASSERT(inserted);
    }

    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_IN, client->sockid);
    event->in.size_B = size_B;
    event->in.data = data;
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);

    net->sockets[client->sockid].lastActivity_us = SkalPlfNow_us();
}
