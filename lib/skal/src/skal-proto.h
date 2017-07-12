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

#ifndef SKAL_PROTO_h_
#define SKAL_PROTO_h_

#ifdef __cplusplus
extern "C" {
#endif


/** skal-proto implements the skal network protocol
 *
 * This module is not MT-safe.
 *
 * This protocol is used to transmit skal messages and blobs over the network.
 * It assumes a UDP-like transport layer, i.e. that packets can be dropped,
 * duplicated or arrive unordered. It also assumes the transport layer does not
 * provide framing (UDP does, but not TCP).
 *
 * It thus handles detection of lost packets, retransmissions, etc. However, it
 * is not optimised for bad networks, as skal is essentially meant to be used
 * over reliable networks for low-latency, high-throughput data flow.
 *
 * skal-proto does not send to or received from sockets directly. The management
 * of sockets must be done by the upper layer. The skal-proto module "sends" and
 * "receives" data through functions and callbacks.
 *
 * skal-proto encodes in the native endianness of the machine sending the data.
 * The "network byte order", which is big-endian, is actually seldom used. All
 * x86-based platorms are little-endian, and in my experience the vast majority
 * of ARM platforms are configured to be little-endian too. skal-proto has a
 * mechanism to detect the endinanness of a message, so it can convert a message
 * to the native endianness of the recipient if the message has a different
 * endinanness.
 *
 * skal-proto encodes and decodes raw data and buffer data accordingly in order
 * to make the life of the upper layer as easy as possible.
 *
 * skal-proto maintains its own socket structures (one such structure per actual
 * socket). Such structures are allocated as and when needed. They must be
 * explicitely de-allocated by a call to `SkalProtoCloseSocket()`, though.
 */


#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Version number of the protocol */
#define SKAL_PROTO_VERSION 1


/** Prototype of a callback function to send a packet
 *
 * The arguments are:
 *  - `sockid`: skal-net socket identifier of where to send the packet
 *  - `data`  : Data to send; always != NULL
 *  - `size_B`: How many bytes to send, always >0 and <mtu_B
 */
typedef void (*SkalProtoSendF)(int sockid, uint8_t* data, int size_B);


/** Prototype of a callback function to notify the reception of a message
 *
 * The arguments are:
 *  - `msg`: The received message, complete with all its blobs
 */
typedef void (*SkalProtoRecvF)(SkalMsg* msg);


/** Structure containing all the configuration parameters of skal-proto */
typedef struct {
    /** Callback to send a packet, must not be NULL */
    SkalProtoSendF send;

    /** Callback to receive a packet, must not be NULL */
    SkalProtoRecvF recv;

    /** Computer unique id */
    int64_t hostid;
} SkalProtoParams;


/** Socket-specific parameters
 *
 * In order to fine-tune the shape of the network traffic, the most important
 * parameter is `bitrate_bps`, followed by `burstRate_bps`.
 */
typedef struct {
    /** Maximum packet size; <=0 for default value */
    int mtu_B;

    /** Retransmit timeout; <=0 for default value
     *
     * How long to wait after a peer last sent some data before asking for a
     * retransmit?
     */
    int retransmitTimeout_ms;

    /** Send queue size; <=0 for default value
     *
     * This is the maximum number of messages that can be queued for sending.
     */
    int sendQueueSize;

    /** Desired output rate, in bytes per second; <=0 for default value */
    int64_t outputRate_Bps;

    /** Max amount of data that can be sent at once; <=0 for default value
     *
     * This is the maximum number of bytes that will be sent through a socket at
     * any one time. It is highly recommended that this value is set to less
     * than the system's local send buffer for that socket.
     *
     * If >0, this value must be >= `mtu_B`.
     */
    int maxBurst_B;
} SkalProtoSocketParams;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise the skal-proto module
 *
 * @param params [in] Configuration parameters; must not be NULL
 */
void SkalProtoInit(const SkalProtoParams* params);


/** De-initialise the skal-proto module */
void SkalProtoExit(void);


/** Allow skal-proto to process its data
 *
 * This gives the opportunity to skal-proto to do something, mostly calculating
 * the next packets to send and send them. This function should be called
 * regularily. This is especially important when sending a large amount of data
 * over a socket.
 *
 * **IMPORTANT** It is highly recommended that this function is called at fairly
 * regular intervals, like every 1ms. It is not recommended to call this
 * function randomly here and there, and especially multiple times in a row.
 */
void SkalProtoProcess(void);


/** Set socket-specific parameters for a socket
 *
 * A socket structure will be created if it does not exist yet.
 *
 * @param sockid [in] Socket that is now open
 * @param params [in] Socket parameters; must not be NULL
 */
void SkalProtoSetSocketParams(int sockid, const SkalProtoSocketParams* params);


/** Notify skal-proto that a socket has been closed
 *
 * Informs skal-proto that the skal-net socket sockid is now closed. All pending
 * data accumulated on this socket will be discarded.
 *
 * @param sockid [in] Socket that is now closed
 */
void SkalProtoCloseSocket(int sockid);


/** Notify skal-proto that some data has been received on a socket
 *
 * @param sockid [in] Socket that received the data
 * @param data   [in] Received data; must not be NULL
 * @param size_B [in] Number of bytes received; must be >0
 */
void SkalProtoDataIn(int sockid, const uint8_t* data, int size_B);


/** Instruct skal-proto to send the given message over the given socket
 *
 * This function takes ownership of `msg`.
 *
 * @param sockid [in] Socket where to send the data
 * @param msg    [in] Message to send; must not be NULL
 *
 * @return `true` if message successfully enqueued, `false` if send queue is
 *         full
 */
bool SkalProtoSendMsg(int sockid, SkalMsg* msg);



#ifdef __cplusplus
}
#endif

#endif /* SKAL_PROTO_h_ */
