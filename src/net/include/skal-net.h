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

#ifndef SKAL_NET_h_
#define SKAL_NET_h_

/** Platform-dependent network encapsulation for SKAL
 *
 * @defgroup skalnet Platform-dependent network encapsulation for SKAL
 * @addtogroup skalnet
 * @{
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


/** The different types of socket available
 *
 * If you know the BSD socket API, this is a combination of address family, type
 * and protocol.
 */
typedef enum {
    /** Unnamed pipes
     *
     * To create a pipe, call `SkalNetServerCreate()` with `type` set to
     * `SKAL_NET_TYPE_PIPE`. The returned sockid will be for reading data from
     * the pipe. It is not possible to write data into the pipe using this
     * sockid.
     *
     * A `SKAL_NET_EV_CONN` event will be generated automatically, and the
     * `SkalNetEventConn.commSockid` will be the sockid for writing data into
     * the pipe. It is not possible to read from the pipe using this sockid.
     */
    SKAL_NET_TYPE_PIPE,

    /** UNIX domain - stream */
    SKAL_NET_TYPE_UNIX_STREAM,

    /** UNIX domain - datagram */
    SKAL_NET_TYPE_UNIX_DGRAM,

    /** UNIX domain - sequential packets */
    SKAL_NET_TYPE_UNIX_SEQPACKET,

    /** IPv4 domain - TCP */
    SKAL_NET_TYPE_IP4_TCP,

    /** IPv4 domain - UDP */
    SKAL_NET_TYPE_IP4_UDP

    // TODO: Add support for SCTP
    // TODO: Add support for IPv6
} SkalNetType;


/** A UNIX address */
typedef struct {
    char path[108];
} SkalNetAddrUnix;


/** An IPv4 address; used for TCP, UDP and SCTP */
typedef struct {
    uint32_t address; // host byte order
    uint16_t port;    // host byte order
} SkalNetAddrIp4;


/** A network address */
typedef union {
    /** UNIX address, for `SKAL_NET_TYPE_UNIX_*` */
    SkalNetAddrUnix unix;

    /** IPv4 address, for `SKAL_NET_TYPE_IP4_*` */
    SkalNetAddrIp4 ip4;
} SkalNetAddr;


/** The different socket events that can happen on a socket set */
typedef enum {
    /** A server socket accepted a connection request from a client
     *
     * Please note that skal-net simulates this behaviour on connection-less
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
     * However, nothing precludes the peer to continue sending packets after
     * this timeout period. If you closed the socket, a new connection-less comm
     * socket will be created as usual. If you didn't close the socket, you will
     * receive `SKAL_NET_EV_IN` events after the `SKAL_NET_EV_DISCONN` event.
     */
    SKAL_NET_EV_DISCONN,

    /** We received data from a peer */
    SKAL_NET_EV_IN,

    /** We can send on a socket */
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
    int         commSockid; /**< Id of newly created comm socket */
    SkalNetAddr peer;       /**< Address of who connected to us */
} SkalNetEventConn;


/** Event extra data: Data received */
typedef struct {
    SkalNetAddr peer;   /**< Address of who sent the data */
    int         size_B; /**< Number of bytes received; always >0 */
    uint8_t*    data;   /**< Received data; never NULL */
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
        SkalNetEventConn conn; /**< For `SKAL_NET_EV_CONN` */
        SkalNetEventIn   in;   /**< For `SKAL_NET_EV_IN` */
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
     * No data has been sent. This can happen only on packet-based sockets,
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
     * This can happen only on connection-based sockets.
     */
    SKAL_NET_SEND_RESET
} SkalNetSendResult;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Convert a string to an IPv4 address
 *
 * @param str [in]  String to parse; must not be NULL
 * @param ip4 [out] Parsed IPv4 address; must not be NULL
 *
 * @return `true` if success, `false` if `str` is not a valid IP address
 */
bool SkalNetStringToIp4(const char* str, uint32_t* ip4);


/** Convert an IPv4 address to a string
 *
 * @param ip4      [in]  IPv4 address to convert
 * @param str      [out] Where to write the result string
 * @param capacity [in]  Capacity if `str`, in char
 */
void SkalNetIp4ToString(uint32_t ip4, char* str, int capacity);


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
 * @param pollTimeout_us [in] Polling timeout, in us; <=0 for default value
 *
 * @return A newly created socket set; this function never returns NULL
 */
SkalNet* SkalNetCreate(int64_t pollTimeout_us);


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
 * When creating a UNIX server socket, you must ensure that `localAddr->path`
 * does not exist, or is a socket and both readable and writeable by the current
 * process.
 *
 * The server socket will be connection-less if `type` is one of the following:
 *  - `SKAL_NET_TYPE_UNIX_DGRAM`
 *  - `SKAL_NET_TYPE_IP4_TCP`
 *
 * For all other types, the socket will be connection-oriented.
 *
 * NB: For UNIX/Linux developers: please note the returned socket id is NOT the
 * socket file descriptor!
 *
 * @param net       [in,out] Socket set to add server socket to; must not be
 *                           NULL
 * @param sntype    [in]     Type of socket to create
 * @param localAddr [in]     Local address to listen to. If `type` is
 *                           `SKAL_NET_TYPE_PIPE`, this argument is ignored and
 *                           may be NULL. For all other `type`; must not be
 *                           NULL.
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
 *                           incoming connection requests; <=0 for default
 *                           value.
 *
 * @return Id of the newly created server socket; this function never fails
 */
int SkalNetServerCreate(SkalNet* net, SkalNetType sntype,
        const SkalNetAddr* localAddr, int bufsize_B, void* context, int extra);


/** Create a comm socket within a socket set
 *
 * Please note the establishement of the connection over the network takes time,
 * and thus is performed asynchronously. Consequently, when this function
 * returns, you can't yet send or receive data through it. You will be notified
 * once the connection has been established by a `SKAL_NET_EV_ESTABLISED` event.
 * If the connection can't be established, you will receive a
 * `SKAL_NET_EV_NOT_ESTABLISED` event.
 *
 * @param net        [in,out] Socket set to add comm socket to; must not be NULL
 * @param sntype     [in]     Type of socket to create; must not be
 *                            `SKAL_NET_TYPE_PIPE`
 * @param localAddr  [in]     Local address to bind to; may be NULL; ignored for
 *                            UNIX sockets
 * @param remoteAddr [in]     Remote address to connect to; must not be NULL
 * @param bufsize_B  [in]     Socket buffer size, in bytes; <=0 for default
 * @param context    [in]     Private context to associate with the comm
 *                            socket; this context will be provided back when
 *                            events occur on this socket for your conveninence
 * @param timeout_us [in]     Idle timeout in us (for connection-less comm
 *                            sockets only); <=0 for default
 *
 * @return Id of the newly created comm socket
 */
int SkalNetCommCreate(SkalNet* net, SkalNetType sntype,
        const SkalNetAddr* localAddr, const SkalNetAddr* remoteAddr,
        int bufsize_B, void* context, int64_t timeout_us);


/** Wait for something to happen
 *
 * This function blocks until something happens on one (or more) socket in the
 * set.
 *
 * @param net [in,out] Socket set to poll; must not be NULL
 *
 * @return The event that occurred, or NULL if timeout; when you are finished
 *         with the event, please call `SkalNetEventUnref()` on it
 */
SkalNetEvent* SkalNetPoll_BLOCKING(SkalNet* net);


/** Assign a context to a socket
 *
 * The previous context value is overwritten. The main purpose of this function
 * is to assign a context to a comm socket automatically created by a server
 * socket when a peer wants to connect to us.
 *
 * @param net     [in,out] Socket set to modify; must not be NULL
 * @param sockid  [in]     Id of the socket to modify
 * @param context [in]     Private context you want to associate with the above
 *                         socket
 *
 * @return `true` if OK, `false` if `sockid` is not valid
 */
bool SkalNetSetContext(SkalNet* net, int sockid, void* context);


/** Mark/unmark a comm socket as having data to be sent through it
 *
 * Calling this function will mark the given comm socket as having data to be
 * sent. The next call to `SkalNetPoll_BLOCKING()` will generate an
 * `SKAL_NET_EV_OUT` event when data can be sent through the socket without
 * blocking. Please note that sending a large buffer might still block depending
 * on the socket buffer size.
 *
 * The above operation is optional and available mainly to efficiently throttle
 * sending of a large amount of data in chunks without blocking the sending
 * thread. You can do that only on a stream-oriented comm socket.
 *
 * The `flag` is not reset automatically, you must call this function again
 * with a `flag` value of `false` to disable this behaviour. If you don't reset
 * the flag and don't send data over the socket, the poll function will always
 * return immediately because the socket can always send data.
 *
 * @param net    [in,out] Socket set to modify; must not be NULL
 * @param sockid [in]     Id of socket to modify; must be a stream-oriented comm
 *                        socket
 * @param flag   [in]     `true` to be notified when socket can send, `false` to
 *                        not be notified.
 *
 * @return `true` if OK, `false` if `sockid` is not valid
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
 * made available.
 *
 * Although this function is marked as blocking, it should not block if you use
 * the `SKAL_NET_EV_OUT` mechanism.
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
        const uint8_t* data, int size_B);


/** Destroy a socket from a socket set
 *
 * @param net    [in,out] Socket set to destroy socket from; must not be NULL
 * @param sockid [in]     Id of the socket to destroy
 *
 * @return `true` if success, `false` if `sockid` it not valid
 */
bool SkalNetSocketDestroy(SkalNet* net, int sockid);



/* @} */
#endif /* SKAL_NET_h_ */
