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
 * bytes in such a structure MUST be set to 0; this is because socket addresses
 * are sometimes used as map keys, and thus unused bytes must not be random.
 *
 * Please note that although skal-net essentially has only one blocking call
 * (which is `SkalNetPoll_BLOCKING()`), the sockets themselves (as file
 * descriptors) are normally blocking. This prevents us from managing EAGAIN
 * situations, and everything is managed through `select(2)` anyway.
 */

#include "skal-net.h"
#include "cdsmap.h"
#include <sys/time.h>
#include <sys/types.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <arpa/inet.h>
#include <netinet/in.h>
#include <netdb.h>
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

    /** Address of peer; also used as the key */
    struct sockaddr_un peerAddr;

    /** Index of client socket in the socket set */
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
     * socket.
     */
    bool isServer;

    /** Comm socket only: Is this socket born of a server socket?
     *
     * A comm socket can either be created out of a server socket when it is
     * created from the server side, or it can be created directly when created
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
     * Items are of type `skalNetCnxLessClientItem`, and keys are
     * `skalNetCnxLessClientItem->peerAddr`.
     */
    CdsMap* cnxLessClients;

    /** Local address for this socket (ignored for pipes) */
    struct sockaddr_un localAddr;

    /** Comm socket: Peer address (ignored for pipes) */
    struct sockaddr_un peerAddr;
} skalNetSocket;


/** Socket set */
struct SkalNet {
    /** Size of the `sockets` array */
    int nsockets;

    /** Array of socket structures
     *
     * Allocating and deallocating sockets is likely to be infrequent.
     * Consequently, we don't use a fancy data structure to hold them, but just
     * a simple, expandable array. This has the significant benefit of being
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


/** De-reference a connection-less client item */
static void skalNetCnxLessClientUnref(CdsMapItem* item);


/** Convert a skal-net URL address to a POSIX address
 *
 * If the URL happens to be a pipe, `posixAddr->sun_family` will be set to -1,
 * `sockType` and `protocol` will be left untouched, and the function returns 0.
 *
 * For IPv4 and IPv6 address families, this function will also resolve the host
 * name using a DNS lookup.
 *
 * @param url       [in]  skal-net URL address to convert; must be a valid URL
 * @param posixAddr [out] POSIX address; must not be NULL
 * @param sockType  [out] Socket type; must not be NULL
 * @param protocol  [out] Socket protocol; must not be NULL
 *
 * @return -1 if `url` is invalid; 0 if `url` is a pipe; the POSIX address
 *         length in all other cases (in bytes)
 */
static int skalNetUrlToPosix(const char* url,
        struct sockaddr_un* posixAddr, int* sockType, int* protocol);


/** Convert a POSIX address to a skal-net URL address
 *
 * The combination of address domain, socket type and protocol must exist for
 * the underlying operating system and must make sense.
 *
 * @param posixAddr [in] POSIX address; must not be NULL
 * @param sockType  [in] Socket type, like `SOCK_STREAM`, `SOCK_DGRAM`, etc.
 * @param protocol  [in] Protocol; can be 0
 *
 * @return URL, never NULL; call `free()` on it when finished
 */
static char* skalNetPosixToUrl(const struct sockaddr_un* posixAddr,
        int sockType, int protocol);


/** Allocate a new event structure
 *
 * The event structure will be reset to 0 and the reference count set to 1.
 *
 * **IMPORTANT**: The event `context` is filled in when the event is popped out,
 * not in this function! Do not set the `context` after calling this function!
 * Please refer to `SkalNetPoll_BLOCKING()` for more information.
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
 * The allocated socket structure is memset to 0, and its `fd` is set to -1.
 *
 * **WARNING** `net->sockets` might be moved by this function!
 *
 * @param net [in,out] Socket set
 *
 * @return Id of the socket just created, which is its index in the socket
 *         array
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
 * @param context   [in]     Upper layer cookie
 *
 * @return Socket id of "server" end of the pipe (i.e. the reading side), or -1
 *         if system error
 */
static int skalNetCreatePipe(SkalNet* net, int bufsize_B, void* context);


/** Run `select(2)` on the sockets and enqueue the corresponding events
 *
 * Events will be enqueued in the `net->events` queue. If `select()` times out,
 * no event will be enqueued.
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


/** Allocate and fill in a new socket structure for a new comm socket from a server socket
 *
 * Call this function when the file descriptor for the client socket has been
 * created already.
 *
 * @param net      [in,out] Socket set where to create the new comm socket; must
 *                          not be NULL
 * @param sockid   [in]     Id of server socket from where the new comm socket
 *                          has been created
 * @param fd       [in]     File descriptor of client socket; must be >=0
 * @param peerAddr [in]     Address of peer; may be NULL for pipes
 *
 * @return Id of new comm socket
 */
static int skalNetNewComm(SkalNet* net, int sockid, int fd,
        const struct sockaddr_un* peerAddr);


/** Receive a packet from a packet-oriented socket
 *
 * The `bufsize_B` field of the given socket is the maximum number of bytes that
 * will be read from the socket. If the actual packet was larger than that, it
 * will be silently truncated and the rest of the packet will be lost.
 *
 * If `peerAddr` is not NULL, it will be filled in with the address returned by
 * `recvfrom(2)`.
 *
 * @param net      [in,out] Socket set; must not be NULL
 * @param sockid   [in]     Id of socket to read from
 * @param size_B   [out]    Number of bytes received; must not be NULL
 * @param peerAddr [out]    Address of peer; may be NULL
 *
 * @return Received data, allocated using `malloc(3)`; please call `free(3)` on
 *         it when finished with it; returns NULL in case of error
 */
static void* skalNetReadPacket(SkalNet* net, int sockid,
        int* size_B, struct sockaddr_un* peerAddr);


/** Send data over a packet-oriented socket
 *
 * A single send will be performed, and if all the data couldn't be sent at
 * once, this function will return `SKAL_NET_SEND_TRUNC`.
 *
 * @param net    [in,out] Socket set; must not be NULL
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
 * This function will make sure all the data is sent. If `size_B` is large, this
 * function will probably block for a significant amount of time until all data
 * is sent.
 *
 * @param net    [in,out] Socket set; must not be NULL
 * @param sockid [in]     Id of socket to send from; must be a stream-oriented
 *                        socket
 * @param data   [in]     Data to send; must not be NULL
 * @param size_B [in]     Number of bytes to send; must be >0
 *
 * @return The send result
 */
static SkalNetSendResult skalNetSendStream(SkalNet* net, int sockid,
        const void* data, int size_B);


/** Handle the reception of data on a connection-less server socket
 *
 * This function will create a new comm socket if `peerAddr` is unknown to this
 * server socket.
 *
 * *IMPORTANT* This function takes ownership of the `data`.
 *
 * @param net      [in,out] Socket set; must not be NULL
 * @param sockid   [in]     Id of server socket; must point to a connection-less
 *                          server socket
 * @param peerAddr [in]     Address of peer that sent this data; must not be
 *                          NULL
 * @param data     [in,out] Buffer containing the data; must not be NULL
 * @param size_B   [in]     Size of above buffer, in bytes; must be >=0
 */
static void skalNetHandleDataOnCnxLessServerSocket(SkalNet* net,
        int sockid, const struct sockaddr_un* peerAddr, void* data, int size_B);



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


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


SkalNet* SkalNetCreate(SkalNetCtxUnref ctxUnref)
{
    SkalNet* net = SkalMallocZ(sizeof(*net));
    net->events = CdsListCreate(NULL, 0, (CdsListItemUnref)SkalNetEventUnref);
    net->ctxUnref = ctxUnref;
    return net;
}


void SkalNetDestroy(SkalNet* net)
{
    SKALASSERT(net != NULL);
    if (net->nsockets > 0) {
        SKALASSERT(net->sockets != NULL);
    }
    for (int sockid = 0; sockid < net->nsockets; sockid++) {
        if (net->sockets[sockid].fd >= 0) {
            SkalNetSocketDestroy(net, sockid);
        }
    }
    CdsListDestroy(net->events);
    free(net->sockets);
    free(net);
}


int SkalNetServerCreate(SkalNet* net, const char* localUrl,
        int bufsize_B, void* context, int extra)
{
    SKALASSERT(net != NULL);

    // Check, parse and resolve `localUrl`
    SKALASSERT(localUrl != NULL);
    struct sockaddr_un localAddr;
    int sockType;
    int protocol;
    int socklen = skalNetUrlToPosix(localUrl, &localAddr, &sockType, &protocol);
    if (socklen < 0) {
        return -1;
    }

    // Special case for pipes
    if (0 == socklen) {
        return skalNetCreatePipe(net, bufsize_B, context);
    }

    // Allocate the socket & fill in the structure
    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* s = &(net->sockets[sockid]);
    s->domain = localAddr.sun_family;
    s->type = sockType;
    s->protocol = protocol;
    s->isServer = true;
    s->isCnxLess = (SOCK_DGRAM == sockType);
    s->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    s->context = context;
    if (s->isCnxLess) {
        if (extra > 0) {
            s->timeout_us = extra;
        } else {
            s->timeout_us = SKAL_NET_DEFAULT_TIMEOUT_us;
        }
        size_t len = sizeof(struct sockaddr_un);
        s->cnxLessClients = CdsMapCreate(
                NULL,                       // name
                0,                          // capacity
                SkalMemCompare,             // compare
                (void*)len,                 // cookie for compare function
                NULL,                       // keyUnref
                skalNetCnxLessClientUnref); // itemUnref
        // NB: The cookie for the `compare` function is the number of bytes to
        // compare, please refer to `SkalMemCompare()` for more information.
    }
    s->localAddr = localAddr;

    // Create the socket
    int fd = socket(localAddr.sun_family, sockType, protocol);
    if (fd < 0) {
        SkalLog("socket(domain=%d, type=%d, protocol=%d) failed: errno=%d [%s] [localUrl=%s]",
                localAddr.sun_family, sockType, protocol,
                errno, strerror(errno), localUrl);
        if (s->cnxLessClients != NULL) {
            CdsMapDestroy(s->cnxLessClients);
            s->cnxLessClients = NULL;
        }
        return -1;
    }
    s->fd = fd;

    // Enable address reuse
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret != -1);

    // Bind the socket
    ret = bind(fd, (const struct sockaddr*)&localAddr, socklen);
    if (ret < 0) {
        SkalLog("bind(%s) failed: errno=%d [%s]",
                localUrl, errno, strerror(errno));
        SkalNetSocketDestroy(net, sockid);
        return -1;
    }

    if (s->isCnxLess) {
        // Set buffer size on connection-less server sockets, as they are used
        // to actually send and receive data (even though skal-net make it look
        // like there are many sockets created from this server socket, they all
        // refer to the same file descriptor).
        ret = setsockopt(fd, SOL_SOCKET, SO_RCVBUF,
                &s->bufsize_B, sizeof(s->bufsize_B));
        SKALASSERT(0 == ret);
        ret = setsockopt(fd, SOL_SOCKET, SO_SNDBUF,
                &s->bufsize_B, sizeof(s->bufsize_B));
        SKALASSERT(0 == ret);

    } else {
        // Place connection-oriented server sockets in listening mode
        int backlog = SKAL_NET_DEFAULT_BACKLOG;
        if (extra > 0) {
            backlog = extra;
        }
        ret = listen(fd, backlog);
        SKALASSERT(0 == ret);
    }

    return sockid;
}


int SkalNetCommCreate(SkalNet* net, const char* localUrl, const char* peerUrl,
        int bufsize_B, void* context, int64_t timeout_us)
{
    SKALASSERT(net != NULL);
    SKALASSERT(peerUrl != NULL);

    // Validate and parse `peerUrl`
    struct sockaddr_un peerAddr;
    int sockType;
    int protocol;
    int socklen = skalNetUrlToPosix(peerUrl, &peerAddr, &sockType, &protocol);
    if (socklen < 0) {
        SkalLog("Pipes are not created by SkalNetCommCreate()");
        return -1;
    }
    SKALASSERT(socklen != 0);

    // Validate and parse `localUrl`
    bool hasLocalAddr = false;
    struct sockaddr_un localAddr;
    if (localUrl != NULL) {
        int sockType2;
        int protocol2;
        int socklen2 = skalNetUrlToPosix(localUrl,
                &localAddr, &sockType2, &protocol2);
        SKALASSERT(socklen2 == socklen);
        SKALASSERT(localAddr.sun_family == peerAddr.sun_family);
        SKALASSERT(sockType2 == sockType);
        if (protocol2 != 0) {
            SKALASSERT(protocol2 == protocol);
        }
        hasLocalAddr = true;
    }

    // Allocate the socket & fill in the structure
    int sockid = skalNetAllocateSocket(net);
    skalNetSocket* c = &(net->sockets[sockid]);
    c->domain = peerAddr.sun_family;
    c->type = sockType;
    c->protocol = protocol;
    if (SOCK_DGRAM == c->type) {
        c->isCnxLess = true;
        if (timeout_us > 0) {
            c->timeout_us = timeout_us;
        } else {
            c->timeout_us = SKAL_NET_DEFAULT_TIMEOUT_us;
        }
        c->lastActivity_us = SkalPlfNow_us();
    }
    c->bufsize_B = skalNetGetBufsize_B(bufsize_B);
    c->context = context;
    // NB: Do not set `c->localAddr` yet as `localUrl` may be NULL
    c->peerAddr = peerAddr;

    // Create the socket
    int fd = socket(c->domain, c->type, c->protocol);
    if (fd < 0) {
        SkalLog("socket(domain=%d, type=%d, protocol=%d) failed: errno=%d [%s] [peer=%s]",
                c->domain, c->type, c->protocol,
                errno, strerror(errno), peerUrl);
        return -1;
    }
    c->fd = fd;

    // Enable address reuse
    int optval = 1;
    int ret = setsockopt(fd, SOL_SOCKET, SO_REUSEADDR, &optval, sizeof(optval));
    SKALASSERT(ret >= 0);

    // Make stream-oriented sockets non-blocking (so the calling thread is not
    // blocked while it is trying to connect)
    if (c->type != SOCK_DGRAM) {
        int flags = fcntl(fd, F_GETFL);
        SKALASSERT(flags != -1);
        flags |= O_NONBLOCK;
        ret = fcntl(fd, F_SETFL, flags);
        SKALASSERT(0 == ret);
    }

    // Bind the socket if requested or if the address family is UNIX. We always
    // bind UNIX sockets, this is to allow the other side of the UNIX socket to
    // distinguish between client sockets when using datagrams.
    if (hasLocalAddr || (AF_UNIX == c->domain)) {
        if (AF_UNIX == c->domain) {
            // Generate a unique path for the comm socket to bind to.
            // NB: We require this so that each connnection-less UNIX comm
            // socket can be distinguished by the server socket in `recvfrom()`.
            // NB: For UNIX sockets, we ignore the user-supplied `localAddr`.
            snprintf(   localAddr.sun_path, sizeof(localAddr.sun_path),
                        "%s%cskal-%d-%016llx-%08lx.sock",
                        SkalPlfTmpDir(),
                        SkalPlfDirSep(),
                        SkalPlfTid(),
                        (unsigned long long)SkalPlfNow_ns(),
                        (unsigned long)SkalPlfRandomU32());
            localAddr.sun_family = AF_UNIX;
        }
        ret = bind(fd, (const struct sockaddr*)&localAddr, socklen);
        if (ret < 0) {
            // NB: `localUrl` may be NULL
            char* url = skalNetPosixToUrl(&localAddr, sockType, protocol);
            SkalLog("bind(%s) failed: errno=%d [%s] [peer=%s]",
                    url, errno, strerror(errno), peerUrl);
            free(url);
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
    ret = connect(fd, (const struct sockaddr*)&peerAddr, socklen);
    if (ret < 0) {
        switch (errno) {
        case ECONNREFUSED :
            {
                // We might get an immediate refusal in the case of UNIX sockets
                // when the socket file exists but no server is listening at the
                // other end.
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
                SkalLog("connect(%s) failed: errno=%d [%s]",
                        peerUrl, errno, strerror(errno));
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

        // Put stream-oriented sockets back in blocking mode
        if (c->type != SOCK_DGRAM) {
            int flags = fcntl(fd, F_GETFL);
            SKALASSERT(flags != -1);
            flags &= ~O_NONBLOCK;
            ret = fcntl(fd, F_SETFL, flags);
            SKALASSERT(0 == ret);
        }
    }

    // Get local address
    socklen_t len = sizeof(c->localAddr);
    ret = getsockname(fd, (struct sockaddr*)&c->localAddr, &len);
    SKALASSERT(0 == ret);
    return sockid;
}


SkalNetEvent* SkalNetPoll_BLOCKING(SkalNet* net)
{
    SKALASSERT(net != NULL);

    SkalNetEvent* event = NULL;
    while (NULL == event) {
        // Scan cnx-less comm sockets for timeouts
        // TODO: This should be optimised so we don't check it every time
        // `SkalNetPoll_BLOCKING()` is called...
        int64_t now_us = SkalPlfNow_us();
        for (int sockid = 0; sockid < net->nsockets; sockid++) {
            skalNetSocket* s = &(net->sockets[sockid]);
            if ((s->fd >= 0) && !(s->isServer) && s->isCnxLess) {
                if ((now_us - s->lastActivity_us) > s->timeout_us) {
                    SkalNetEvent* evdisconn = skalNetEventAllocate(
                            SKAL_NET_EV_DISCONN, sockid);
                    bool inserted = CdsListPushBack(net->events, &evdisconn->item);
                    SKALASSERT(inserted);

                    // Wait for `timeout_us` before sending again
                    // `SKAL_NET_EV_DISCONN` to the upper layer
                    s->lastActivity_us = now_us;
                }
            }
        }

        // Try to pop an event from the queue
        event = (SkalNetEvent*)CdsListPopFront(net->events);
        if (NULL == event) {
            // No event in the queue, wait for something to happen.
            // NB: `skalNetSelect()` can timeout and thus will not enqueue any
            // event; this is intentional, as it allows us to run again the code
            // above to check for timeouts in connection-less sockets.
            skalNetSelect(net);
            event = (SkalNetEvent*)CdsListPopFront(net->events);
        }

        if (event != NULL) {
            // We have an event! But we need to update its `context` before
            // forwarding it to the upper layer.
            if (net->sockets[event->sockid].fd < 0) {
                // Socket has been closed by upper layer after the event has
                // been generated.
                //  => Silently drop the event
                SkalNetEventUnref(event);
                event = NULL;
            } else {
                // We fill in the context at the last minute, in case the upper
                // layer changes (or sets) it between the time the event is
                // generated and now.
                event->context = net->sockets[event->sockid].context;
            }
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
        void* old = net->sockets[sockid].context;
        if ((old != NULL) && (net->ctxUnref != NULL)) {
            net->ctxUnref(old);
        }
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

    if ((sockid < 0) || (sockid >= net->nsockets)) {
        SkalLog("Invalid sockid %d; should be >=0 and < %d\n",
                sockid, net->nsockets);
        return;
    }

    skalNetSocket* s = &(net->sockets[sockid]);
    if (s->fd >= 0) {
        bool canClose = true;
        if (s->isCnxLess) {
            // For connection-less server sockets, the fd is shared between the
            // server and all the clients created out of this server.
            // Consequently, we close the underlying file descriptor when the
            // last socket using it is closed.
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
            unlink(s->localAddr.sun_path);
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


static void skalNetCnxLessClientUnref(CdsMapItem* item)
{
    free(item);
}


static int skalNetUrlToPosix(const char* url,
        struct sockaddr_un* posixAddr, int* sockType, int* protocol)
{
    SKALASSERT(url != NULL);
    SKALASSERT(posixAddr != NULL);
    SKALASSERT(sockType != NULL);
    SKALASSERT(protocol != NULL);

    // Default protocol to 0, as it is a sensible value
    *protocol = 0;

    // Pipes
    if (strncasecmp(url, "pipe://", 7) == 0) {
        posixAddr->sun_family = -1;
        return 0;
    }

    // UNIX sockets
    if (strncasecmp(url, "unix", 4) == 0) {
        posixAddr->sun_family = AF_UNIX;
        const char* path = NULL;
        if (strncasecmp(url, "unix://", 7) == 0) {
            *sockType = SOCK_SEQPACKET;
            path = url + 7;
        } else if (strncasecmp(url, "unixs://", 8) == 0) {
            *sockType = SOCK_STREAM;
            path = url + 8;
        } else if (strncasecmp(url, "unixd://", 8) == 0) {
            *sockType = SOCK_DGRAM;
            path = url + 8;
        } else {
            SkalLog("Invalid URL '%s': unknown scheme", url);
            return -1;
        }
        SKALASSERT(path != NULL);
        int n = snprintf(posixAddr->sun_path, sizeof(posixAddr->sun_path),
                "%s", path);
        if (n >= (int)sizeof(posixAddr->sun_path)) {
            SkalLog("Invalid URL '%s': UNIX socket path too long", url);
            return -1;
        }
        return sizeof(*posixAddr);
    }

    // TCP or UDP sockets: setup DNS resolution hints
    struct addrinfo hints;
    memset(&hints, 0, sizeof(hints));
    const char* ptr = NULL;
    if (strncasecmp(url, "tcp://", 6) == 0) {
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_STREAM;
        hints.ai_protocol = IPPROTO_TCP;
        ptr = url + 6;
    } else if (strncasecmp(url, "udp://", 6) == 0) {
        hints.ai_family = AF_INET;
        hints.ai_socktype = SOCK_DGRAM;
        hints.ai_protocol = IPPROTO_UDP;
        ptr = url + 6;
    } else {
        SkalLog("Invalid URL '%s': unknown scheme", url);
        return -1;
    }

    // Separate the host part from the port part
    if (strchr(ptr, ':') == NULL) {
        SkalLog("Invalid URL '%s': can't find ':' character", url);
        return -1;
    }
    char* host = SkalStrdup(ptr);
    char* port = strchr(host, ':');
    SKALASSERT(port != NULL);
    *port = '\0';
    port++;

    // Lookup host and port (which can be a string representing a service, like
    // "domain", "daytime" or "http").
    // NB: We only take the first returned result, which should be acceptable
    // given the `hints`.
    struct addrinfo* results = NULL;
    int ret = getaddrinfo(host, port, &hints, &results);
    if (ret != 0) {
        if (EAI_SYSTEM == ret) {
            SkalLog("Failed to resolve URL '%s': %s [%d]",
                    url, strerror(errno), errno);
        } else {
            SkalLog("Failed to resolve URL '%s': %s [%d]",
                    url, gai_strerror(ret), ret);
        }
        free(host);
        return -1;
    }
    SKALASSERT(results != NULL);
    SKALASSERT(results->ai_family == hints.ai_family);
    SKALASSERT(results->ai_socktype == hints.ai_socktype);
    SKALASSERT(results->ai_protocol == hints.ai_protocol);
    memset(posixAddr, 0, sizeof(*posixAddr)); // Ensure unused bytes are 0
    ret = results->ai_addrlen;
    SKALASSERT(ret > 0);
    memcpy(posixAddr, results->ai_addr, ret);
    *sockType = results->ai_socktype;
    *protocol = results->ai_protocol;

    // Done
    freeaddrinfo(results);
    free(host);
    return ret;
}


static char* skalNetPosixToUrl(const struct sockaddr_un* posixAddr,
        int sockType, int protocol)
{
    SKALASSERT(posixAddr != NULL);

    switch (posixAddr->sun_family) {
    case AF_UNIX :
        switch (sockType) {
        case SOCK_STREAM:
            return SkalSPrintf("unixs://%s", posixAddr->sun_path);
        case SOCK_DGRAM :
            return SkalSPrintf("unixd://%s", posixAddr->sun_path);
        case SOCK_SEQPACKET :
            return SkalSPrintf("unix://%s", posixAddr->sun_path);
        default :
            SKALPANIC_MSG("Unhandled socket type %d", sockType);
        }
        break;

    case AF_INET :
        {
            const struct sockaddr_in* sin =(const struct sockaddr_in*)posixAddr;
            const char* scheme = NULL;
            if (IPPROTO_SCTP == protocol) {
                switch (sockType) {
                case SOCK_SEQPACKET :
                    scheme = "sctp";
                    break;
                case SOCK_STREAM :
                    scheme = "sctps";
                    break;
                case SOCK_DGRAM :
                    SKALPANIC_MSG("SCTP protocol does not support datagrams");
                    break;
                default :
                    SKALPANIC_MSG("Unhandled socket type %d", sockType);
                }
            } else {
                switch (sockType) {
                case SOCK_STREAM :
                    scheme = "tcp";
                    break;
                case SOCK_DGRAM :
                    scheme = "udp";
                    break;
                default :
                    SKALPANIC_MSG("Unhandled socket type %d", sockType);
                }
            }
            char ip[INET_ADDRSTRLEN];
            const char* ret = inet_ntop(AF_INET,
                    &sin->sin_addr, ip, sizeof(ip));
            SKALASSERT(ret != NULL);
            unsigned long port = ntohs(sin->sin_port);
            return SkalSPrintf("%s://%s:%lu", scheme, ip, port);
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

    // Reset the socket structure
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

    // Set pipe buffer size (this is done on the writing end of the pipe)
    if (s->bufsize_B > 0) {
        int ret = fcntl(fds[1], F_SETPIPE_SZ, s->bufsize_B);
        SKALASSERT(ret >= 0);
    }

    // Spawn a new comm socket for the "writing" end of the pipe
    (void)skalNetNewComm(net, sockid, fds[1], NULL);

    // Done
    return sockid;
}


static void skalNetSelect(SkalNet* net)
{
    // Fill in fd sets
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
    tv.tv_sec  = SKAL_NET_POLL_TIMEOUT_us / 1000000LL;
    tv.tv_usec = SKAL_NET_POLL_TIMEOUT_us % 1000000LL;
    int count = select(maxFd + 1, &readfds, pWriteFds, &exceptfds, &tv);
    if (count < 0) {
        SKALASSERT(EINTR == errno);
        // If we were interrupted by a signal, just behave like we timed out
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
        // This is a server socket
        if (s->isCnxLess) {
            // We received data on a connection-less server socket
            // NB: A connection-less socket is necessarily packet-based
            int size_B;
            struct sockaddr_un peerAddr;
            memset(&peerAddr, 0, sizeof(peerAddr));
            void* data = skalNetReadPacket(net, sockid, &size_B, &peerAddr);
            if (data != NULL) {
                SKALASSERT(size_B > 0);
                skalNetHandleDataOnCnxLessServerSocket(net, sockid,
                        &peerAddr, data, size_B);
            }
        } else if (s->domain < 0) {
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
        } else {
            // This is a regular connection-oriented server socket and we
            // received a connection request from a client
            skalNetAccept(net, sockid);
        }
    } else {
        // This is a comm socket
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
        const struct sockaddr_un* peerAddr)
{
    SKALASSERT(net != NULL);
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
    if (c->isCnxLess) {
        c->timeout_us = s->timeout_us;
        c->lastActivity_us = SkalPlfNow_us();
    }
    if (peerAddr != NULL) {
        c->peerAddr = *peerAddr;
    }

    if (c->domain >= 0) {
        socklen_t len = sizeof(c->localAddr);
        int ret = getsockname(c->fd, (struct sockaddr*)&c->localAddr, &len);
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
        int* size_B, struct sockaddr_un* peerAddr)
{
    SKALASSERT(net != NULL);
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < net->nsockets);
    SKALASSERT(size_B != NULL);

    skalNetSocket* c = &(net->sockets[sockid]);
    SKALASSERT(c->fd >= 0);
    SKALASSERT(c->bufsize_B > 0);
    void* data = SkalMalloc(c->bufsize_B);

    socklen_t socklen = sizeof(*peerAddr);
    int ret = recvfrom(c->fd, data, c->bufsize_B, 0, peerAddr, &socklen);

    if ((0 == ret) || ((ret < 0) && (ECONNRESET == errno))) {
        // NB: Various domains (UNIX, IPv4, etc.) allow empty datagrams, but we
        // assume no empty datagram is ever sent to us. We instead assume an
        // empty read means the connection has been closed (for example for
        // SOCK_SEQPACKET sockets).
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
        char* url = skalNetPosixToUrl(&c->localAddr, c->type, c->protocol);
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
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < net->nsockets);
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
                (const struct sockaddr*)&c->peerAddr, socklen);
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
                    char* url = skalNetPosixToUrl(&c->peerAddr,
                            c->type, c->protocol);
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
            // Peer has disconnected
            // Send the `EV_DISCONN` event only if we read nothing. `select()`
            // will still return immediately with a "input" event.
            done = true;
            if (readSoFar_B <= 0) {
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
            // NB: Please note the `MSG_NOSIGNAL` flag, so no SIGPIPE signal is
            // generated if the peer has closed the connection.
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
                // Unexpected error
                SkalLog("Unexpected errno while sending on a stream socket: %s [%d]",
                        strerror(errno), errno);
                result = SKAL_NET_SEND_ERROR;
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
        int sockid, const struct sockaddr_un* peerAddr, void* data, int size_B)
{
    SKALASSERT(net != NULL);
    SKALASSERT((sockid >= 0) && (sockid < net->nsockets));
    SKALASSERT(peerAddr != NULL);
    SKALASSERT(data != NULL);
    SKALASSERT(size_B >= 0);

    skalNetSocket* s = &(net->sockets[sockid]);
    SKALASSERT(s->fd >= 0);
    SKALASSERT(s->isServer);
    SKALASSERT(s->isCnxLess);

    skalNetCnxLessClientItem* client = (skalNetCnxLessClientItem*)
        CdsMapSearch(s->cnxLessClients, (void*)peerAddr);
    if (NULL == client) {
        // This is the first time we are receiving data from this client
        int commSockid = skalNetNewComm(net, sockid, s->fd, peerAddr);
        // NB: Careful! The previous call might have re-allocated `net->sockets`

        client = SkalMallocZ(sizeof(*client));
        client->peerAddr = *peerAddr;
        client->sockid = commSockid;
        bool inserted = CdsMapInsert(net->sockets[sockid].cnxLessClients,
                &client->peerAddr, &client->item);
        SKALASSERT(inserted);
    }

    SkalNetEvent* event = skalNetEventAllocate(SKAL_NET_EV_IN, client->sockid);
    event->in.size_B = size_B;
    event->in.data = data;
    bool inserted = CdsListPushBack(net->events, &event->item);
    SKALASSERT(inserted);

    net->sockets[client->sockid].lastActivity_us = SkalPlfNow_us();
}
