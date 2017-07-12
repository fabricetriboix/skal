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

#include "skal-proto.h"
#include "skal-msg.h"
#include "skal-blob.h"
#include "skal-alarm.h"
#include "cdslist.h"
#include "cdsmap.h"
#include <string.h>


/** Format of a packet
 *
 *    +-----------+
 *    | ABVT LLLL |
 *    | BLOC K-ID |  Header
 *    | NNNN CCCC |
 *    +-----------+
 *    | -PAY LOAD |  Payload
 *    +-----------+
 *
 * Format of a packet:
 *  - The first 2 bytes are "AB" in hex: 0x4142; this is used to detect
 *    the message endianness
 *  - The next byte is:
 *    * High bit: 1 if there is a header extension (otherwise 0)
 *    * Next 7 bits: the protocol version, as a uint8_t
 *  - The next byte is the packet type; the possible packet types are:
 *    * 0x01: msg data; this packet contains a payload that is part of a msg
 *    * 0x02: blob data; this packet contains a payload that is part of a blob
 *    * 0x41: retransmit request; the sender wants the peer to re-transmit
 *            packet number NNNN
 *    * 0x81: error: no such block; this may be sent in response to a retransmit
 *            request when the original block does not exist anymore
 *  - The next 4 bytes is the packet length (including the header), in bytes,
 *    as a uint32_t
 *  - The next 8 bytes is the block id, as a uint64_t
 *  - The next 4 bytes is the packet number, as a uint32_t
 *  - The next 4 bytes is the packet count for that block, as a uint32_t
 *  - The rest is the payload
 *
 * A block can be reconstructed by concatenating the payloads in the order
 * indicated by the packet numbers. The recipient knows how many packets must be
 * received to reconstruct the block thanks to the CCCC field which is the
 * number of packets for this block.
 *
 * Missing packets are detected and retransmission requests are issued. It is
 * assumed the packets are sent in order (although they can be dropped or
 * re-ordered by the network, skal-proto assumes this would be a rare event).
 */



/*----------------+
 | Macros & Types |
 +----------------*/


/** How many socket structures to add at once */
#define SKAL_PROTO_SOCKETS_INCREMENT 8


/** Structure used to temporarily hold a message in serialized form */
typedef struct {
    CdsMapItem item;
    int      ref;           /**< Reference counter */
    uint64_t blockid;       /**< Block id for this message */
    uint8_t* msgData;       /**< Serialised message data */
    int64_t  msgDataSize_B; /**< Size of the above data, in bytes */
    int64_t  packetNumber;  /**< Number of packet to send next */
} skalProtoTxMsg;


/** Socket */
typedef struct {
    /** Socket id; redundant but useful */
    int sockid;

    /** Socket parameters */
    SkalProtoSocketParams params;

    /** Time this socket has last been processed */
    int64_t timeLastProcessed_us;

    /** Queue of `SkalMsg` to send */
    CdsList* msgToSend;

    /** Message being transmitted; NULL if no msg is being transmitted */
    skalProtoTxMsg* txMsg;

    /** Messages that have been transmitted
     *
     * This is a map of `skalProtoTxMsg`; the key is `skalProtoTxMsg.blockid`.
     *
     * After the last packet of a message is sent, the message will be kept for
     * a certain time in this map in order to be able to answer retransmit
     * requests.
     */
    CdsMap* sentMsg;
} skalProtoSocket;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Get a socket structure, creating it if it doesn't exist yet
 *
 * @param sockid [in] Id of socket to get
 *
 * @return Socket structure; this function never returns NULL
 */
static skalProtoSocket* skalProtoGetSocket(int sockid);


/** Process a socket
 *
 * This function will check for what needs to be done next and will do it.
 *
 * @param skt    [in,out] Socket to Process; must not be NULL
 * @param now_us [in]     Current time
 */
static void skalProtoProcessSocket(skalProtoSocket* skt, int64_t now_us);


/** Send the next batch of packets for the current message
 *
 * If there is no current message, or if the rest of the current message can be
 * sent and we have some capacity left, this function will pull the next message
 * in the queue (if any) and will start sending its packets.
 *
 * @param skt   [in,out] Socket where to send the current msg; must not be NULL
 * @param max_B [in]     Maximum number of bytes to send; must not be NULL
 */
static int64_t skalProtoProcessMsg(skalProtoSocket* skt, int64_t max_B);


/** Serialize a message
 *
 * @param msg [in] Msg to serialize; must not be NULL
 *
 * @return Structure to transmit the serialized message; please call
 *         `skalProtoTxMsgUnref()` on it when finished
 */
skalProtoTxMsg* skalProtoSerializeMsg(const SkalMsg* msg);



/*------------------+
 | Global variables |
 +------------------*/


/** skal-proto parameters */
static SkalProtoParams gParams;


/** Sockets */
static skalProtoSocket** gSockets = NULL;
static int gSocketCount = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalProtoInit(const SkalProtoParams* params)
{
    SKALASSERT(NULL == gSocketCount);
    SKALASSERT(0 == gSocketCount);

    SKALASSERT(params != NULL);
    SKALASSERT(params->send != NULL);
    SKALASSERT(params->recv != NULL);

    gParams = *params;
}


void SkalProtoExit(void)
{
    for (int i = 0; i < gSocketCount; i++) {
        SkalProtoCloseSocket(i);
    }
    free(gSockets);
    gSockets = NULL;
    gSocketCount = 0;
}


void SkalProtoProcess(void)
{
    int64_t now_us = SkalPlfNow_us();

    // TODO: This should be optimised. Looping through all sockets every time is
    // a waste of time. Use an ordered list of events instead, order by time for
    // the next check
    for (int i = 0; i < gSocketCount; i++) {
        if (gSockets[i] != NULL) {
            skalProtoProcessSocket(gSockets[i], now_us);
        }
    }
}


void SkalProtoCloseSocket(int sockid)
{
    SKALASSERT(gSockets != NULL);
    SKALASSERT(sockid >= 0);
    SKALASSERT(sockid < gSocketCount);

    if (gSocketCount[sockid] != NULL) {
        CdsListDestroy(gSockets[sockid]->sendQueue);
        free(gSockets[sockid]);
    }
}


void SkalProtoDataIn(int sockid, const uint8_t* data, int size_B)
{
    // TODO
}


bool SkalProtoSendMsg(int sockid, SkalMsg* msg)
{
    SKALASSERT(msg != NULL);
    skalProtoSocket* skt = skalProtoGetSocket(sockid);
    return CdsListPushBack(skt->sendQueue, (CdsListItem*)msg);
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static skalProtoSocket* skalProtoGetSocket(int sockid)
{
    SKALASSERT(sockid >= 0);

    if (gSocketCount <= sockid) {
        // Socket array too small => grow it
        int newCount = sockid + SKAL_PROTO_SOCKETS_INCREMENT;
        gSockets = SkalRealloc(gSockets, sizeof(*gSockets) * newCount);
        for (int i = gSocketCount; i < newCount; i++) {
            gSockets[i] = NULL;
        }
        gSocketCount = newCount;
    }

    if (NULL == gSockets[sockid]) {
        // This socket structure does not exist => create it
        gSockets[sockid] = SkalMallocZ(sizeof(**gSockets));
        skalProtoSocket* skt = gSockets[sockid];
        skt->sockid = sockid;
        skt->params.mtu_B = SKAL_PROTO_DEFAULT_MTU_B;
        skt->params.retransmitTimeout_ms = SKAL_PROTO_DEFAULT_MTU_B;
        skt->params.sendQueueSize = SKAL_PROTO_DEFAULT_SEND_QUEUE_SIZE;
        skt->params.bitrate_bps = SKAL_PROTO_DEFAULT_BITRATE_bps;
        skt->params.burstRate_bps = SKAL_PROTO_DEFAULT_BURST_RATE_bsp;
        skt->sendQueue = CdsListCreate(NULL, skt->params.sendQueueSize,
                (void(*)(CdsListItem*))SkalMsgUnref);
    }

    return gSockets[sockid];
}


static void skalProtoProcessSocket(skalProtoSocket* skt, int64_t now_us)
{
    SKALASSERT(skt != NULL);

    int64_t delta_us = now_us - skt->timeLastProcessed_us;
    if (delta_us <= 0) {
        return;
    }
    skt->timeLastProcessed_us = now_us;

    int64_t max_B = (skt->params.bitrate_bps * delta_us) / 8000000LL;
    if (max_B > (int64_t)skt->maxBurst_B) {
        max_B = (int64_t)skt->maxBurst_B;
    }
    if (max_B < skt->params.mtu_B) {
        max_B = skt->params.mtu_B; // Send at least a MTU worth of data
    }

    // TODO: Handle retransmit requests first

    max_B -= skalProtoProcessMsg(skt, max_B);

    // TODO: blobs
}


static int64_t skalProtoProcessMsg(skalProtoSocket* skt, int64_t max_B)
{
    SKALASSERT(skt != NULL);

    int64_t sent_B = 0;
    // While we can send data and there is some message data to send, send it
    bool finished = false;
    while (    !finished && ((sent_B + skt->params.mtu_B) < max_B)
            && ((skt->txMsg != NULL) || !CdsListIsEmpty(skt->msgToSend))) {
        if (skt->txMsg != NULL) {
            // We are currently transmitting a message
            //  => Send the next packet
            int64_t offset_B = skt->txMsg->packetNumber * skt->params.mtu_B;
            uint8_t* pkt = skt->txMsg->msgData + offset_B;

            int64_t remaining_B = skt->txMsg->msgDataSize_B - offset_B;
            bool lastPacket;
            int64_t size_B;
            if (remaining_B <= skt->params.mtu_B) {
                lastPacket = true;
                size_B = remaining_B;
            } else {
                lastPacket = false;
                size_B = skt->params.mtu_B;
            }

            gParams.send(skt->sockid, pkt, size_B);
            sent_B += size_B;
            (skt->packetNumber)++;
            if (lastPacket) {
                // This message has been sent in full
                //  => Move it to the "sent" map
                bool inserted = CdsMapInsert(skt->sentMsg,
                        &(skt->txMsg->blockid), (CdsMapItem*)skt->txMsg);
                SKALASSERT(inserted);
                skt->txMsg = NULL;
            }

        } else if (!CdsListIsEmpty(skt->msgToSend)) {
            // We are not currently transmitting a message, and we have more
            // messages to send.
            SkalMsg* msg = (SkalMsg*)CdsListPopFront(skt->msgToSend);
            skt->txMsg = skalProtoSerializeMsg(msg);
            SkalMsgUnref(msg);

        } else {
            // Nothing more to send
            finished = true;
        }
    } // while we can send data and there is data to be sent

    return sent_B;
}


skalProtoTxMsg* skalProtoSerializeMsg(const SkalMsg* msg)
{
    skalProtoTxMsg* txMsg = SkalMallocZ(sizeof(*txMsg));
    txMsg->ref = 1;
    txMsg->blockid = ; // TODO
    txMsg->msgData = SkalMsgSerialize(msg, &txMsg->msgDataSize_B);
    txMsg->packetNumber = 0;
    return txMsg;
}
