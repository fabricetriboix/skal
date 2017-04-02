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

#ifndef SKAL_NET_h_
#define SKAL_NET_h_

/** Platform-dependent network encapsulation for SKAL
 *
 * @defgroup skalnet Platform-dependent network encapsulation for SKAL
 * @addtogroup skalnet
 * @{
 *
 * The skal-net module is not MT-safe (unless noted otherwise). This module
 * works on a `SkalNet` object, which is essentially a set of sockets. To use
 * this module, just create a `SkalNet` object, and create sockets on it. There
 * are essentially 2 kinds of sockets:
 *  - "server" socket: this is used to accept incoming connections; please note
 *    that connection-less socket types (like for the UDP protocol) still have a
 *    concept of "server" socket, this is all emulated in skal-net to provide a
 *    unifed interface
 *  - "comm" socket: this is a socket which can send and receive data; it can
 *    be created either from a server socket (when a peer wants to connect), or
 *    when you when to connect to a peer server
 *  - In addition, skal-net has support for unnamed pipes, which behave like
 *    "comm" sockets, except that one end can only receive data, and the other
 *    can only send data.
 *
 * Functions that can potentially block have their names ending in "_BLOCKING",
 * to make you aware of that fact.
 *
 * The skal-net module uses URL representations of socket addresses; this is a
 * combination of family domain, socket type, protocol and socket address. The
 * best way to understand it is probably by way of examples:
 *  - "tcp://10.1.2.3:http": IPv4 TCP socket, bound to address 10.1.2.3, port 80
 *  - "tcp6://google.com:7": IPv6 TCP socket, bound to whatever "google.com"
 *    resolves to, port 7
 *  - "udp://127.0.0.1:9001": IPv4 UDP socket, bound to localhost, port 9001
 *  - "unix:///tmp/my.sock": UNIX socket of type SEQPACKET, path "/tmp/my.sock"
 *  - "unixs:///tmp/xyz": UNIX socket of type SOCK_STREAM, path "/tmp/xyz"
 *  - "unixd://local.sock" UNIX socket of type SOCK_DGRAM, path "local.sock"
 *    (so a relative path, if that's useful at all)
 *  - "pipe://": an unnamed pipe, as in `pipe(2)`
 *
 * Some comments:
 *  - UNIX sockets are of type SOCK_SEQPACKET by default, use "unixs://" to
 *    create a UNIX socket of type SOCK_STREAM, and "unixd://" to create a UNIX
 *    socket of type SOCK_DGRAM
 *  - It is possible to create pipes with "pipe://" and to use them as if they
 *    were sockets (except that one end of the pipe can only be written to, and
 *    the other end can only be read from)
 *  - When a host name is specified instead of a numerical address, a DNS lookup
 *    is performed to resolve the host name
 */

#include "skalcommon.h"
#include "cdslist.h"



/*----------------+
 | Types & Macros |
 +----------------*/


/** Opaque type representing a set of sockets
 *
 * Please note that all functions operating on a `SkalNet` object are expected
 * to be called from within a single thread. In other words, they are not
 * MT-Safe. There is an exception detailed in `SkalNetSend_BLOCKING()`.
 */
typedef struct SkalNet SkalNet;


/** The different socket events that can happen on a socket set */
typedef enum {
    /** A server socket accepted a connection request from a client
     *
     * Please note that skal-net emulates this behaviour on connection-less
     * sockets as well. When data is received from an unknown peer, a
     * connection-less comm socket is created for exclusive communication with
     * that peer and a `SKAL_NET_EV_CONN` event is generated. That socket will
     * be closed when there is no activity on that socket for a certain amount
     * of time, in which case a `SKAL_NET_EV_DISCONN` event is generated.
     */
    SKAL_NET_EV_CONN,

    /** A peer disconnected from an established connection
     *
     * It is recommended you destroy this socket whenever convenient.
     *
     * NB: In the case of a connection-less comm socket, this event is just an
     * indication that no activity took place during `timeout_us`. Hence this
     * socket is considered inactive and should be closed.
     *
     * However, nothing precludes the peer to continue sending and receiving
     * packets after this timeout period. If data is received after you closed
     * the socket, a new connection-less comm socket will be created as usual.
     * If you didn't close the socket, you will receive `SKAL_NET_EV_IN` events
     * after the `SKAL_NET_EV_DISCONN` event.
     */
    SKAL_NET_EV_DISCONN,

    /** We received data from a peer */
    SKAL_NET_EV_IN,

    /** We can now send on a socket without blocking */
    SKAL_NET_EV_OUT,

    /** A comm socket has established a connection to its server */
    SKAL_NET_EV_ESTABLISHED,

    /** A comm socket can't establish a connection to its server
     *
     * This is probably because the server can't be reached or the requested
     * port is not open.
     *
     * It is recommended you destroy this socket whenever convenient.
     */
    SKAL_NET_EV_NOT_ESTABLISHED,

    /** The OS reported an error on the given socket
     *
     * It is recommended you destroy this socket whenever convenient.
     */
    SKAL_NET_EV_ERROR
} SkalNetEventType;


/** Event extra data: Connection accepted */
typedef struct {
    int commSockid; /**< Id of newly created comm socket */
} SkalNetEventConn;


/** Event extra data: Data received */
typedef struct {
    int   size_B; /**< Number of bytes received; always >0 */
    void* data;   /**< Received data; never NULL */
} SkalNetEventIn;


/** Structure representing an event
 *
 * Please be careful: in the case of a `SKAL_NET_EV_CONN`, `sockid` will be the
 * id of the server socket, not of the new comm socket.
 */
typedef struct {
    CdsListItem item; /**< Private, do not touch! */
    int         ref;  /**< Private, do not touch! */

    SkalNetEventType type;    /**< Event type */
    int              sockid;  /**< Id of socket that originated the event */
    void*            context; /**< Your private context for above socket */
    union {
        SkalNetEventConn conn; /**< Extra data for `SKAL_NET_EV_CONN` */
        SkalNetEventIn   in;   /**< Extra data for `SKAL_NET_EV_IN` */
    };
} SkalNetEvent;


/** Possible return values for `SkalNetSend()` */
typedef enum
{
    /** Success, all the data has been sent successfully */
    SKAL_NET_SEND_OK,

    /** The `sockid` argument points to a non-existent or a server socket */
    SKAL_NET_SEND_INVAL_SOCKID,

    /** The packet was too big to be sent atomically
     *
     * No data has been sent. This can happen only on packet-based sockets
     * where the chosen underlying protocol requires packets to be sent
     * atomically.
     */
    SKAL_NET_SEND_TOO_BIG,

    /** You tried to send too much data and it has been truncated
     *
     * This can happen only on packet-based sockets.
     */
    SKAL_NET_SEND_TRUNC,

    /** Connection has been reset by peer while we were sending
     *
     * This can happen only on connection-oriented sockets. It is recommended
     * you destroy this socket whenever convenient.
     */
    SKAL_NET_SEND_RESET,

    /** Unexpected error
     *
     * This should not happen and is likely to be a bug in skal-net...
     */
    SKAL_NET_SEND_ERROR
} SkalNetSendResult;


/** Prototype of a function to de-reference a socket context */
typedef void (*SkalNetCtxUnref)(void* ctx);



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Add a reference to an event object */
void SkalNetEventRef(SkalNetEvent* event);


/** Remove a reference from the event object
 *
 * Once the last reference is removed, the event object is freed. You should
 * always assume you are removing the last reference, and therefore that the
 * object has been freed.
 */
void SkalNetEventUnref(SkalNetEvent* event);


/** Create a socket set
 *
 * @param ctxUnref [in] Function to call to de-reference a socket context; may
 *                      be NULL if not needed
 *
 * @return A newly created socket set; this function never returns NULL
 */
SkalNet* SkalNetCreate(SkalNetCtxUnref ctxUnref);


/** Destroy a socket set
 *
 * All sockets still open in the set will be destroyed.
 *
 * @param net [in,out] Socket set to destroy; must not be NULL
 */
void SkalNetDestroy(SkalNet* net);


/** Create a server socket within a socket set
 *
 * If created successfully, the server socket will become part of the `net`
 * socket set.
 *
 * When creating a UNIX server socket, you must ensure that its path points to a
 * non-existent file, or that the file it points to is a socket file and both
 * readable and writeable by the current process.
 *
 * The server socket will be connection-less for datagram-based socket types,
 * and connection-oriented for everything else.
 *
 * NB: For UNIX/Linux developers: please note the returned socket id is NOT the
 * socket file descriptor!
 *
 * @param net       [in,out] Socket set to add server socket to; must not be
 *                           NULL
 * @param localUrl  [in]     Local address to listen to; must be a valid URL
 * @param bufsize_B [in]     Buffer size for comm sockets created out of this
 *                           server socket, in bytes; <=0 for default value
 * @param context   [in]     Private context to associate with the server
 *                           socket; this context will be provided back when
 *                           events occur on this socket for your convenience
 * @param extra     [in]     If the server socket is connection-less, this is
 *                           the time in us after which an idle comm socket
 *                           issued from this server socket is closed; <=0 for
 *                           default value.
 *                           If the server socket is connection-oriented, this
 *                           is the maximum pending connections before denying
 *                           incoming connection requests; 0 for default value.
 *
 * @return Id of the newly created server socket, or -1 in case of system error
 *         or invalid `localUrl`.
 */
int SkalNetServerCreate(SkalNet* net, const char* localUrl,
        int bufsize_B, void* context, int extra);


/** Create a comm socket within a socket set
 *
 * Please note the establishement of the connection over the network takes time,
 * and thus is performed asynchronously. Consequently, when this function
 * returns, you can't yet send or receive data through it. You will be notified
 * once the connection has been established by a `SKAL_NET_EV_ESTABLISED` event.
 * If the connection can't be established for any reason, you will receive a
 * `SKAL_NET_EV_NOT_ESTABLISED` event.
 *
 * @param net        [in,out] Socket set to add comm socket to; must not be NULL
 * @param localUrl   [in]     Local address to bind to; may be NULL; if not
 *                            NULL, must have the same type and protocol as
 *                            `peerUrl`; ignored for UNIX sockets
 * @param peerUrl    [in]     Remote address to connect to; must be a valid URL;
 *                            must not be "pipe://"
 * @param bufsize_B  [in]     Socket buffer size, in bytes; 0 for default
 * @param context    [in]     Private context to associate with the comm socket;
 *                            this context will be provided back when events
 *                            occur on this socket for your conveninence
 * @param timeout_us [in]     Idle timeout in us (for connection-less comm
 *                            sockets only, ignored for other sockets); 0 for
 *                            default
 *
 * @return Id of the newly created comm socket, or -1 in case of a system error
 *         or invalid URL(s)
 */
int SkalNetCommCreate(SkalNet* net, const char* localUrl, const char* peerUrl,
        int bufsize_B, void* context, int64_t timeout_us);


/** Wait for something to happen
 *
 * This function blocks until something happens in the socket set.
 *
 * @param net [in,out] Socket set to poll; must not be NULL
 *
 * @return The event that occurred; this function never returns NULL; the
 *         ownership of the event is transferred to you, so please call
 *         `SkalNetEventUnref()` when you're finished with it
 */
SkalNetEvent* SkalNetPoll_BLOCKING(SkalNet* net);


/** Assign a context to a socket
 *
 * If the socket currently has a context which is not NULL, and if the
 * unreferencing function `SkalNetCtxUnref` (set when `SkalNetCreate()` was
 * called) is not null, the current context will be unreferenced.
 *
 * The main purpose of this function is to assign a context to a comm socket
 * automatically created by a server socket when a peer wants to connect to us.
 *
 * @param net     [in,out] Socket set to modify; must not be NULL
 * @param sockid  [in]     Id of the socket to modify
 * @param context [in]     Private context you want to associate with the above
 *                         socket
 *
 * @return `true` if OK, `false` if `sockid` is not valid
 */
bool SkalNetSetContext(SkalNet* net, int sockid, void* context);


/** Get the context associated with a socket
 *
 * @param net     [in] Socket set to query; must not be NULL
 * @param sockid  [in] Id of the socket to query
 *
 * @return The associated context, or NULL if `sockid` is not valid
 */
void* SkalNetGetContext(const SkalNet* net, int sockid);


/** Mark/unmark a comm socket as having data to be sent through it
 *
 * Calling this function will mark the given comm socket as having data to be
 * sent. The next call to `SkalNetPoll_BLOCKING()` will generate an
 * `SKAL_NET_EV_OUT` event when data can be sent through the socket without
 * blocking. Please note that sending a large buffer in one go might still block
 * depending on the socket buffer size and how full the socket buffer currently
 * is.
 *
 * The above operation is optional and available mainly to efficiently throttle
 * sending large amounts of data in chunks without blocking the sending thread.
 * This makes sense only on a stream-oriented comm socket (like TCP). A
 * connection-less socket (like UDP) will simply see packets dropped by the
 * local machine, the peer, or any router/switch in between.
 *
 * The `flag` is not reset automatically, you must call this function again
 * with a `flag` value of `false` to disable this behaviour. If you don't reset
 * the flag and don't send data over the socket, the poll function will always
 * return immediately because the socket can always send data.
 *
 * When a socket is created, this behaviour is disabled by default.
 *
 * @param net    [in,out] Socket set to modify; must not be NULL
 * @param sockid [in]     Id of socket to modify; should be a stream-oriented
 *                        comm socket
 * @param flag   [in]     `true` to be notified when socket can send without
 *                        blocking, `false` to not be notified
 *
 * @return `true` if OK, `false` if `sockid` is not a stream-oriented comm
 *         socket
 */
bool SkalNetWantToSend(SkalNet* net, int sockid, bool flag);


/** Send data through a comm socket
 *
 * Please note this function may block depending on the size of the socket
 * buffer and the number of bytes being sent.
 *
 * You may call this function from outside the thread that is calling all the
 * other `SkalNet*()` functions for that socket set, provided the following
 * conditions are met:
 *  - A call to `SkalNetDestroy()` is not in progress and will not start
 *    until this call finishes
 *  - A call to `SkalNetSocketDestroy()` for the same `sockid` is not in
 *    progress and will not start until this call finishes
 *
 * *DESIGN NOTE*: Because you can be asynchronously notified that a socket is
 * available for sending, a non-blocking version of this function has not been
 * implemented.
 *
 * Although this function is marked as blocking, it should not block if you use
 * the `SKAL_NET_EV_OUT` mechanism and send small enough buffers.
 *
 * @param net    [in,out] Socket set; must not be NULL
 * @param sockid [in]     Id of comm socket to send from; must point to a comm
 *                        socket
 * @param data   [in]     Data to send; must not be NULL
 * @param size_B [in]     Number of bytes to send; must be >0
 *
 * @return Result of send operation
 */
SkalNetSendResult SkalNetSend_BLOCKING(SkalNet* net, int sockid,
        const void* data, int size_B);


/** Destroy a socket from a socket set
 *
 * @param net    [in,out] Socket set to destroy socket from; must not be NULL
 * @param sockid [in]     Id of the socket to destroy; must be valid
 */
void SkalNetSocketDestroy(SkalNet* net, int sockid);



/* @} */
#endif /* SKAL_NET_h_ */
