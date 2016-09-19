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

#ifndef SKAL_MSG_h_
#define SKAL_MSG_h_

#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Version number of the message format */
#define SKAL_MSG_VERSION 1


/** Message flag: super-urgent message; reserved for SKAL internal use */
#define SKAL_MSG_IFLAG_INTERNAL 0x80



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Add a reference to a message
 *
 * This will increment the message reference counter by one. If blobs are
 * attached to the message, their reference counters are also incremented.
 *
 * @param msg [in,out] Message to reference; must not be NULL
 */
void SkalMsgRef(SkalMsg* msg);


/** Set a message's internal flags
 *
 * @param msg [in,out] Message to manipulate; must not be NULL
 */
void SkalMsgSetInternalFlags(SkalMsg* msg, uint8_t flags);


/** Reset a message's internal flags
 *
 * @param msg [in,out] Message to manipulate; must not be NULL
 */
void SkalMsgResetInternalFlags(SkalMsg* msg, uint8_t flags);


/** Get a message's internal flags
 *
 * @param msg [in] Messaage to query
 *
 * @return The message internal flags
 */
uint8_t SkalMsgInternalFlags(const SkalMsg* msg);


/** Encode a message in JSON
 *
 * If any blob is attached to the message, please note they will only be
 * referenced by their ids in the JSON string. If the message is to be sent over
 * the network, the content of each blob must be passed using a separate mean.
 *
 * @param msg [in] Message to encode
 *
 * @return The JSON string representing the message; this function never returns
 *         NULL. Once finished with the JSON string, you must release it by
 *         calling `free()` on it.
 */
char* SkalMsgToJson(const SkalMsg* msg);


/** Create a message from a JSON string
 *
 * The created message will be a partial message if it contains any blob. Since
 * the blobs are not part of the JSON, they must be reconstructed outside this
 * function. Until all the blobs attached to this message are reconstructed,
 * this message will remain partial and can't be sent to anyone.
 *
 * @param json [in] JSON string to parse
 *
 * @return The newly created SKAL message, with its reference counter set to 1,
 *         or NULL if the JSON string is not valid.
 */
// TODO SkalMsg* SkalMsgCreateFromJson(const char* json);


/** DEBUG: Get the number of message references in this process
 *
 * This is a debug/testing function.
 */
int64_t SkalMsgRefCount_DEBUG(void);



#endif /* SKAL_MSG_h_ */
