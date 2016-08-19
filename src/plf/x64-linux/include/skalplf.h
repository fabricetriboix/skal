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

#ifndef SKAL_PLF_h_
#define SKAL_PLF_h_

/** Platform-dependent stuff for SKAL
 *
 * @defgroup skalplf Platform-dependent stuff for SKAL
 * @addtogroup skalplf
 * @{
 */

#include "skalcfg.h"
#include <stdlib.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>



/*----------------+
 | Types & Macros |
 +----------------*/


/** Panic macro */
#define SKALPANIC \
    do { \
        fprintf(stderr, "SKAL PANIC at %s:%d\n", __FILE__, __LINE__); \
        abort(); \
    } while (0)


/** Panic macro with message */
#define SKALPANIC_MSG(_fmt, ...) \
    do { \
        fprintf(stderr, "SKAL PANIC: "); \
        fprintf(stderr, (_fmt), ## __VA_ARGS__); \
        fprintf(stderr, " (at %s:%d)\n", __FILE__, __LINE__); \
        abort(); \
    } while (0)


/** Assert macro */
#define SKALASSERT(_cond) \
    do { \
        if (!(_cond)) { \
            fprintf(stderr, "SKAL ASSERT: %s (at %s:%d)\n", #_cond, \
                    __FILE__, __LINE__); \
            abort(); \
        } \
    } while (0)


/** Opaque type representing a bare mutex
 *
 * Please note that if you follow the skal framework, you should not use any
 * mutex in your code. You might want to use mutexes in exceptional
 * circumstances, typically because it would be difficult to integrate a
 * third-party software without them.
 *
 * Mutexes are evil. Always try to design your code so that you don't need them.
 */
typedef struct SkalPlfMutex SkalPlfMutex;


/** Opaque type representing a bare condition variable
 *
 * Please note that if you follow the skal framework, you should not use any
 * condition variable in your code. You might want to use condition variables in
 * exceptional circumstances, typically because it would be difficult to
 * integrate a third-party software without them.
 *
 * Condition variables are evil. Always try to design your code so that you
 * don't need them.
 */
typedef struct SkalPlfCondVar SkalPlfCondVar;


/** Opaque type representing a bare thread */
typedef struct SkalPlfThread SkalPlfThread;


/** Function implementing a thread
 *
 * The `arg` argument is the same as the one passed to
 * `SkalPlfThreadCreate()`.
 */
typedef void (*SkalPlfThreadFunction)(void* arg);


/** Opaque type representing a set of sockets
 *
 * Please note that all functions operating on a `SkalPlfNet` object are
 * expected to be called from within a single thread. In other words, they are
 * not MT-Safe. There is an exception detailed in `SkalPlfNetSend()`.
 */
typedef struct SkalPlfNet SkalPlfNet;


/** The different types of socket available
 *
 * If you know the BSD socket API, this is a combination of address family, type
 * and protocol.
 */
typedef enum {
    /** Unnamed pipes
     *
     * To create a pipe, call `SkalPlfNetServerCreate()` with `type` set to
     * `SKAL_PLF_NET_TYPE_PIPE`. The returned sockid will be for reading data
     * from the pipe. It is not possible to write data into the pipe using this
     * sockid.
     *
     * A `SKAL_PLF_NET_EVENT_CONN` event will be generated automatically, and
     * the `SkalPlfNetEventConn.cnxSockid` will be the sockid for writing data
     * into the pipe. It is not possible to read from the pipe using this
     * sockid.
     */
    SKAL_PLF_NET_TYPE_PIPE,

    /** UNIX domain - stream */
    SKAL_PLF_NET_TYPE_UNIX_STREAM,

    /** UNIX domain - datagram */
    SKAL_PLF_NET_TYPE_UNIX_DGRAM,

    /** UNIX domain - sequential packets */
    SKAL_PLF_NET_TYPE_UNIX_SEQPACKET,

    /** IPv4 domain - TCP */
    SKAL_PLF_NET_TYPE_IP4_TCP,

    /** IPv4 domain - UDP */
    SKAL_PLF_NET_TYPE_IP4_UDP

    // TODO: Add support for SCTP
    // TODO: Add support for IPv6
} SkalPlfNetType;


/** A UNIX address */
typedef struct {
    char path[108];
} SkalPlfNetAddrUnix;


/** An IPv4 address; used for TCP, UDP and SCTP */
typedef struct {
    uint32_t address; // host byte order
    uint16_t port;    // host byte order
} SkalPlfNetAddrIp4;


/** A network address */
typedef union {
    /** UNIX address, for `SKAL_PLF_NET_TYPE_UNIX_*` */
    SkalPlfNetAddrUnix unix;

    /** IPv4 address, for `SKAL_PLF_NET_TYPE_IP4_*` */
    SkalPlfNetAddrIp4 ip4;
} SkalPlfNetAddr;


/** The different socket events that can happen on a socket set */
typedef enum {
    /** A server socket accepted a connection request from a client */
    SKAL_PLF_NET_EVENT_CONN,

    /** A peer disconnected from an established connection */
    SKAL_PLF_NET_EVENT_DISCONN,

    /** We received data from a peer */
    SKAL_PLF_NET_EVENT_IN,

    /** We can send on a socket (the socket buffer is not full anymore) */
    SKAL_PLF_NET_EVENT_OUT
} SkalPlfNetEventType;


/** Event structure: Connection accepted */
typedef struct {
    int            sockid;    /**< Id of server socket which accepted the cnx */
    void*          context;   /**< Your private context for the above socket */
    int            cnxSockid; /**< Id of newly created cnx socket */
    SkalPlfNetAddr peer;      /**< Address of who connected to us */
} SkalPlfNetEventConn;


/** Event structure: Disconnection */
typedef struct {
    int   sockid;  /**< Id of cnx socket that is now disconnected */
    void* context; /**< Your private context for the above socket */
} SkalPlfNetEventDisconn;


/** Event structure: Data received */
typedef struct {
    int            sockid;  /**< Id of cnx socket that received data */
    void*          context; /**< Your private context for the above socket */
    uint8_t*       data;    /**< Pointer to received data; never NULL */
    int            size_B;  /**< Number of bytes received; always >0 */
    SkalPlfNetAddr peer;    /**< Address of who sent the data */
} SkalPlfNetEventIn;


/** Event structure: Can now send on socket */
typedef struct {
    int   sockid;  /**< Id of cnx socket where sending is now non-blocking */
    void* context; /**< Your private context for the above socket */
} SkalPlfNetEventOut;


/** All events combined */
typedef struct {
    SkalPlfNetEventType type;
    union {
        SkalPlfNetEventConn    conn;    /**< For `SKAL_PLF_NET_EVENT_CONN` */
        SkalPlfNetEventDisconn disconn; /**< For `SKAL_PLF_NET_EVENT_DISCONN` */
        SkalPlfNetEventIn      in;      /**< For `SKAL_PLF_NET_EVENT_IN` */
    };
} SkalPlfNetEvent;


/** Possible return values for `SkalPlfNetSend()` */
typedef enum
{
    /** Success, all the data has been sent successfully */
    SKAL_PLF_NET_SEND_OK,

    /** The `sockid` argument points to a non-existent or non-cnx socket */
    SKAL_PLF_NET_SEND_INVAL_SOCKID
} SkalPlfNetSendResult;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise this module */
void SkalPlfInit(void);


/** De-initialise this module */
void SkalPlfExit(void);


/** Generate a random string
 *
 * @param buffer [out] Where to write the random string
 * @param size_B [in]  Number of bytes to generate
 */
void SkalPlfRandom(uint8_t* buffer, int size_B);


/** Generate a random 32-bit number */
static inline uint32_t SkalPlfRandomU32(void)
{
    uint32_t x;
    SkalPlfRandom((uint8_t*)&x, sizeof(x));
    return x;
}


/** Generate a random 64-bit number */
static inline uint64_t SkalPlfRandomU64(void)
{
    uint64_t x;
    SkalPlfRandom((uint8_t*)&x, sizeof(x));
    return x;
}


/** Get the current time
 *
 * This is a time that increments linearly in reference to an external clock.
 * Thus it is not influenced by daylight savings time shifts, time zone changes,
 * date or time changes, etc.
 *
 * On Linux, this is the time elapsed since the last boot.
 */
int64_t SkalNow_ns();


/** Create a mutex
 *
 * @return A newly created mutex; this function never returns NULL
 */
SkalPlfMutex* SkalPlfMutexCreate(void);


/** Destroy a mutex
 *
 * No thread should be waiting on this mutex when this function is called.
 *
 * @param mutex [in,out] Mutex to destroy; must not be NULL
 */
void SkalPlfMutexDestroy(SkalPlfMutex* mutex);


/** Lock a mutex
 *
 * NB: Recursive locking is not supported.
 *
 * @param mutex [in,out] Mutex to lock; must not be NULL
 */
void SkalPlfMutexLock(SkalPlfMutex* mutex);


/** Unlock a mutex
 *
 * @param mutex [in,out] Mutex to unlock; must not be NULL
 */
void SkalPlfMutexUnlock(SkalPlfMutex* mutex);


/** Create a condition variable
 *
 * @return A newly created condition variable; this function never returns NULL
 */
SkalPlfCondVar* SkalPlfCondVarCreate(void);


/** Destroy a condition variable
 *
 * @param condvar [in,out] Condition variable to destroy; must not be NULL
 */
void SkalPlfCondVarDestroy(SkalPlfCondVar* condvar);


/** Wait on a condition variable
 *
 * @param condvar [in,out] Condition variable to wait on; must not be NULL
 * @param mutex   [in,out] Mutex associated with the condition variable; must
 *                         not be NULL
 */
void SkalPlfCondVarWait(SkalPlfCondVar* condvar, SkalPlfMutex* mutex);


/** Wake up one thread currently waiting on a condition variable
 *
 * @param condvar [in,out] Condition variable to signal
 */
void SkalPlfCondVarSignal(SkalPlfCondVar* condvar);


/** Create a thread
 *
 * The thread starts immediately.
 *
 * @param name     [in] Name of the new thread; may be NULL if no name needed
 * @param threadfn [in] Function that implements the thread; must not be NULL
 * @param arg      [in] Argument to pass to `threadfn`
 *
 * @return The newly created thread; this function never returns NULL
 */
SkalPlfThread* SkalPlfThreadCreate(const char* name,
        SkalPlfThreadFunction threadfn, void* arg);


/** Cancel a thread
 *
 * Once this function returns, please call `SkalPlfThreadJoin()` on it to free
 * up its resources.
 *
 * @param thread [in,out] Thread to cancel
 */
void SkalPlfThreadCancel(SkalPlfThread* thread);


/* Join a thread
 *
 * If the `thread` is still running when this function is called, it will block
 * until the thread terminates.
 *
 * All resources associated with this thread are freed once this function
 * returns.
 *
 * @param thread [in,out] Thread to join
 */
void SkalPlfThreadJoin(SkalPlfThread* thread);


/** Set the current thread name
 *
 * @param name [in] New name for current thread
 */
void SkalPlfThreadSetName(const char* name);


/** Get the name of the current thread
 *
 * @param buffer [out] Where to write the current thread's name; must not be
 *                     NULL
 * @param size   [in]  Size of the above buffer, in chars
 */
void SkalPlfThreadGetName(char* buffer, int size);


/** Set the thread-specific value
 *
 * Please note only one such value can be held for each thread. If you call this
 * function a second time, the new value will silently overwrite the old value.
 *
 * @param value [in] Value to set, specific to the current thread
 */
void SkalPlfThreadSetSpecific(void* value);


/** Get the thread-specific value
 *
 * @return The thread-specific value, or NULL if it was not set
 */
void* SkalPlfThreadGetSpecific(void);


/** Convert a string to an IPv4 address
 *
 * @param str [in]  String to parse; must not be NULL
 * @param ip4 [out] Parsed IPv4 address; must not be NULL
 *
 * @return `true` if success, `false` if `str` is not a valid IP address
 */
bool SkalPlfStringToIp4(const char* str, uint32_t* ip4);


/** Convert an IPv4 address to a string
 *
 * @param ip4      [in]  IPv4 address to convert
 * @param str      [out] Where to write the result string
 * @param capacity [in]  Capacity if `str`, in char
 */
void SkalPlfIp4ToString(uint32_t ip4, char* str, int capacity);


/** Create a socket set
 *
 * @return A newly created socket set; this function never returns NULL
 */
SkalPlfNet* SkalPlfNetCreate(void);


/** Destroy a socket set
 *
 * All sockets still in the set will be destroyed.
 *
 * @param net [in,out] Socket set to destroy; must not be NULL
 */
void SkalPlfNetDestroy(SkalPlfNet* net);


/** Create a server socket within a socket set
 *
 * If created successfully, the server socket will become part of the `net`
 * socket set.
 *
 * @param net       [in,out] Socket set to add server socket to; must not be
 *                           NULL
 * @param type      [in]     Type of socket to create
 * @param localAddr [in]     Local address to listen to; may be NULL if `type`
 *                           is `SKAL_PLF_NET_TYPE_PIPE`, must not be NULL
 *                           otherwise
 * @param bufsize_B [in]     Default maximum buffer size for cnx sockets created
 *                           out of this server socket, in bytes; <=0 for
 *                           default value
 * @param context   [in]     Private context to associate with the server
 *                           socket; this context will be provided back when
 *                           events occur on this socket
 *
 * @return A socket id, or <0 if error; UNIX/Linux developers: please note the
 *         returned socket id is NOT the socket file descriptor!
 */
int SkalPlfNetServerCreate(SkalPlfNet* net, SkalPlfNetType type,
        const SkalPlfNetAddr* localAddr, int bufsize_B, void* context);


/** Create a cnx socket within a socket set
 *
 * @param net        [in,out] Socket set to add cnx socket to; must not be NULL
 * @param type       [in]     Type of socket to create; must not be
 *                            `SKAL_PLF_NET_TYPE_PIPE`
 * @param localAddr  [in]     Local address to bind to; may be NULL
 * @param remoteAddr [in]     Remote address to connect to; must not be NULL
 * @param bufsize_B  [in]     Maximum buffer size, in bytes; <=0 for default
 * @param context    [in]     Private context to associate with the cnx
 *                            socket; this context will be provided back when
 *                            events occur on this socket
 */
int SkalPlfNetCnxCreate(SkalPlfNet* net, SkalPlfNetType type,
        const SkalPlfNetAddr* localAddr, const SkalPlfNetAddr* remoteAddr,
        int bufsize_B, void* context);


/** Wait for something to happen
 *
 * This function blocks until something happens on one of the socket in the set.
 *
 * @param net [in,out] Socket set to poll; must not be NULL
 *
 * @return The event that occurred; this function never returns NULL; the
 *         returned event structure is valid until the next call to
 *         `SkalPlfNetPoll_BLOCKING()`.
 */
const SkalPlfNetEvent* SkalPlfNetPoll_BLOCKING(SkalPlfNet* net);


/** Assign a context to a socket
 *
 * The previous context value is overwritten.
 *
 * @param net     [in,out] Socket set to modify; must not be NULL
 * @param sockid  [in]     Id of the socket to modify
 * @param context [in]     Private context you want to associate with the above
 *                         socket
 *
 * @return `true` if OK, `false` if `sockid` is not valid
 */
bool SkalPlfNetSetContext(SkalPlfNet* net, int sockid, void* context);


/** Set the maximum buffer size of a socket
 *
 * @param net       [in,out] Socket set to modify; must not be NULL
 * @param sockid    [in]     Id of socket to modify
 * @param bufsize_B [in]     New buffer size, or <= for default value
 *
 * @return `true` if OK, `false` if `sockid` is not valid
 */
bool SkalPlfNetSetBufsize_B(SkalPlfNet* net, int sockid, int bufsize_B);


/** Mark a cnx socket as having data to be sent through it
 *
 * Calling this function will mark the given cnx socket as having data to be
 * sent. The next call to `SkalPlfNetPoll_BLOCKING()` will generate an
 * `SKAL_PLF_NET_EVENT_OUT` event when data can be sent through the socket
 * without blocking. Please note that sending a large buffer might still block
 * depending on the socket buffer size.
 *
 * The above operation is optional and available mainly to efficiently throttle
 * sending of a large amount of data in chunks without blocking the sending
 * thread.
 *
 * @param net    [in,out] Socket set to modify; must not be NULL
 * @param sockid [in]     Id of socket to modify
 *
 * @return `true` if OK, `false` if `sockid` is not valid
 */
bool SkalPlfNetMarkSendPending(SkalPlfNet* net, int sockid);


/** Send data from a cnx socket
 *
 * Please note this function may block depending on the size of the socket
 * buffer and the number of bytes being sent.
 *
 * You may call this function from outside the thread that is calling all the
 * other `SkalPlfNet*()` functions for that socket set, provided the following
 * conditions are met:
 *  - A call to `SkalPlfNetDestroy()` is not in progress and will not start
 *    until this call finishes
 *  - A call to `SkalPlfNetSocketDestroy()` for the same `sockid` is not in
 *    progress and will not start until this call finishes
 *
 * *DESIGN NOTE*: There are 3 cases possible when sending data over a socket:
 *  - Intra-process: The socket is a pipe, and it is expected SKAL will only
 *    send a single byte for each message (because messages are stored in a
 *    skal-queue)
 *  - Inter-process: The socket is a UNIX socket between a skal-master and a
 *    skald: large data will be attached to messages as blobs, so the data
 *    travelling through the UNIX socket will be pretty small
 *  - Inter-machine: The socket is a UDP/TCP/SCTP socket between 2 skalds on
 *    different machines; large data will be attached to messages as blobs and
 *    transmitted in chunks by making use of the `SKAL_PLF_NET_EVENT_OUT`
 *    mechanism
 *
 * The conclusion is that blocking sends are acceptable.
 *
 * @param net    [in,out] Socket set
 * @param sockid [in]     Id of cnx socket to send from
 * @param data   [in]     Data to send
 * @param size_B [in]     Number of bytes to send
 *
 * @return Result of send operation
 */
SkalPlfNetSendResult SkalPlfNetSend_MAYBLOCK(SkalPlfNet* net, int sockid,
        const uint8_t* data, int size_B);


/** Destroy a socket from a socket set
 *
 * @param net    [in,out] Socket set to destroy the socket from
 * @param sockid [in]     Socket id of the socket to destroy
 */
void SkalPlfNetSocketDestroy(SkalPlfNet* net, int sockid);



/* @} */
#endif /* SKAL_PLF_h_ */
