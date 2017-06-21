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

#ifndef SKAL_MSG_h_
#define SKAL_MSG_h_

#ifdef __cplusplus
extern "C" {
#endif


#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Version number of the message format */
#define SKAL_MSG_VERSION 1


/** Message flag: super-urgent message; reserved for SKAL internal use */
#define SKAL_MSG_IFLAG_INTERNAL 0x01



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise the skal-msg module */
void SkalMsgInit(void);


/** De-initialise the skal-msg module */
void SkalMsgExit(void);


/** Set the message sender
 *
 * You should use this function with extreme caution, as the sender is set
 * automatically when the message is created. Calling this function essentially
 * makes the message pretends it has been sent by other thread.
 *
 * @param msg    [in,out] Message to manipulate; must not be NULL
 * @param sender [in]     New sender value; must not be NULL
 */
void SkalMsgSetSender(SkalMsg* msg, const char* sender);


/** Set a message's internal flags
 *
 * @param msg    [in,out] Message to manipulate; must not be NULL
 * @param iflags [in]     Internal flags to set
 */
void SkalMsgSetIFlags(SkalMsg* msg, uint8_t iflags);


/** Reset a message's internal flags
 *
 * @param msg    [in,out] Message to manipulate; must not be NULL
 * @param iflags [in]     Internal flags to reset
 */
void SkalMsgResetIFlags(SkalMsg* msg, uint8_t iflags);


/** Get a message's internal flags
 *
 * @param msg [in] Messaage to query
 *
 * @return The message internal flags
 */
uint8_t SkalMsgIFlags(const SkalMsg* msg);


/** Encode a message in JSON
 *
 * If any blob is attached to the message, please note they will only be
 * referenced by their ids in the JSON string. If the message is to be sent over
 * the network, the content of each blob must be passed using a separate mean.
 *
 * **VERY IMPORTANT** If blobs are attached to this message and you want the
 * lifespan of the blobs to exceed the lifespan of the message structure itself
 * (for example because you want to send the JSON to another thread), you must
 * call `SkalMsgRefBlobs()` prior to sending the message. The recipient should
 * call `SkalMsgUnrefBlobs()` once the JSON is successfully parsed.
 *
 * @param msg [in] Message to encode; must not be NULL
 *
 * @return The JSON string representing the message; this function never returns
 *         NULL. Once finished with the JSON string, you must release it by
 *         calling `free()` on it.
 */
char* SkalMsgToJson(const SkalMsg* msg);


/** Add a reference to all blobs in the message
 *
 * @param msg [in] Message containing the blobs to reference; must not be NULL
 */
void SkalMsgRefBlobs(const SkalMsg* msg);


/** Create a message from a JSON string
 *
 * The created message will be a partial message if it contains any blob. Since
 * the blobs are not part of the JSON, they must be reconstructed outside this
 * function. Until all the blobs attached to this message are reconstructed,
 * this message will remain partial and can't be sent to anyone.
 *
 * Once all the blobs have been received and the message is complete, please
 * call `SkalMsgUnrefBlobs()` to remove the extra reference to the blobs used
 * during the message's transit.
 *
 * @param json [in] JSON string to parse; must not be NULL
 *
 * @return The newly created SKAL message, with its reference counter set to 1,
 *         or NULL if the JSON string is not valid.
 */
SkalMsg* SkalMsgCreateFromJson(const char* json);


/** Remove a reference to all blobs in the message
 *
 * @param msg [in] Message containing the blobs to unreference; must not be NULL
 */
void SkalMsgUnrefBlobs(const SkalMsg* msg);


/** Set the domain name for this process
 *
 * @param domain [in] Domain name; must not be NULL
 */
void SkalSetDomain(const char* domain);


/** DEBUG: Get the number of message references in this process
 *
 * This is a debug/testing function.
 */
int64_t SkalMsgRefCount_DEBUG(void);



#ifdef __cplusplus
}
#endif

#endif /* SKAL_MSG_h_ */
