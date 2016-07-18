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


/** Message flag: super-urgent message; reserved for SKAL internal use */
#define SKAL_MSG_FLAG_SUPER_URGENT 0x80


/** Opaque type to a message queue */
typedef struct SkalQueue SkalQueue;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Create a JSON string that represents the content of the message
 *
 * If any blob is attached to the message, please note they will only be
 * referenced by their ids in the JSON string. The content of each blob must be
 * passed using a separate mean.
 */
// TODO: from here


/** Create a message from a JSON string
 *
 * The created message will be a partial message if it contains any blob. Since
 * the blobs are not part of the JSON, they must be reconstructed outside this
 * function. Until all the blobs attached to this message are reconstructed,
 * this message will remain partial and can't be sent to anyone.
 *
 * \param json [in] JSON string to parse
 *
 * \return The newly created SKAL message, with its reference counter set to 1,
 *         or NULL if the JSON string is not valid.
 */
SkalMsg* SkalMsgCreateFromJson(const char* json);


/** Create a message queue
 *
 * \param threshold [in] When to return `false` when enqueuing a message;
 *                       must be > 0
 *
 * \return The created message queue; this function never returns NULL
 */
SkalQueue* SkalQueueCreate(int64_t threshold);


/** Set the queue in shutdown mode
 *
 * Once in shutdown mode, the queue will not accept any more item being pushed
 * into it. This is used when the thread owning the message queue has to be
 * terminated.
 */
void SkalQueueShutdown(SkalQueue* queue);


/** Destroy a message queue
 *
 * You *MUST* have called `SkalQueueShutdown()` first.
 *
 * All pending messages will be silently dropped.
 *
 * \param queue [in] Message queue to destroy; must not be NULL
 */
void SkalQueueDestroy(SkalQueue* queue);


/** Push a message into the queue
 *
 * If the queue is in shutdown mode, no action is taken and this function
 * returns -1.
 *
 * Otherwise, the following happens. You will lose ownership of the `msg`.
 * Please note this function always succeeds in inserting the message into the
 * queue. It may return 1 is the number of messages it holds (after pushing this
 * one), is greater or equal to its threshold, as set by the `SkalQueueCreate()`
 * function.
 *
 * The message will be pushed into the queue thus:
 *  - If the `SKAL_MSG_FLAG_SUPER_URGENT` is set, the message will be pushed at
 *    the front of the queue
 *  - If the `SKAL_MSG_FLAG_URGENT` is set, the message will be pushed after all
 *    urgent messages, but before all regular messages
 *  - Otherwise, the message will be pushed at the back of the queue
 *
 * \param queue [in,out] Where to push the message; must not be NULL
 * \param msg   [in,out] Message to push; must not be NULL
 *
 * \return 0 if queue is not full, 1 if it's full, -1 if it's in shutdown mode
 */
int SkalQueuePush(SkalQueue* queue, SkalMsg* msg);


/** Pop a message from the queue
 *
 * This is a blocking function! Actually, this is the only blocking function in
 * the whole SKAL framework! If the queue is not empty, this function will pop
 * the message currently at the front of the queue. If the queue is empty, this
 * function will block until a message is pushed into it.
 *
 * \param SkalQueue [in,out] From where to pop a message
 *
 * \return The popped message; this function never returns NULL
 */
SkalMsg* SkalQueuePop_BLOCKING(SkalQueue* queue);



#endif /* SKAL_MSG_h_ */
