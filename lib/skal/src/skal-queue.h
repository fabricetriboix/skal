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

#ifndef SKAL_QUEUE_h_
#define SKAL_QUEUE_h_

/** skal-queue
 *
 * Please note all calls related to `SkalQueue` are MT-safe.
 */

#ifdef __cplusplus
extern "C" {
#endif


#include "skal.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Opaque type to a message queue */
typedef struct SkalQueue SkalQueue;


/** Prototype for a hook function */
typedef void (*SkalQueueHook)(void* cookie);



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Create a message queue
 *
 * @param name      [in] Queue name; must not be NULL
 * @param threshold [in] Queue threshold; must be >0
 *
 * @return The created message queue; this function never returns NULL
 */
SkalQueue* SkalQueueCreate(const char* name, int64_t threshold);


/** Attach a hook to `SkalQueuePush()`
 *
 * Any previous hook will be overwritten.
 *
 * @param queue  [in,out] Queue to attach the hook to; must not be NULL
 * @param hook   [in]     Hook callback; may be NULL to cancel any hook
 * @param cookie [in]     Cookie for the previous callback
 */
void SkalQueueSetPushHook(SkalQueue* queue, SkalQueueHook hook, void* cookie);


/** Get the queue name
 *
 * @param queue [in] Queue to query
 *
 * @return The queue name as set in `SkalQueueCreate()`
 */
const char* SkalQueueName(const SkalQueue* queue);


/** Destroy a message queue
 *
 * All pending messages will be silently dropped.
 *
 * @param queue [in] Message queue to destroy; must not be NULL
 */
void SkalQueueDestroy(SkalQueue* queue);


/** Push a message into the queue
 *
 * You will lose ownership of the `msg`. This function always succeeds in
 * inserting the message into the queue.
 *
 * @param queue [in,out] Where to push the message; must not be NULL
 * @param msg   [in,out] Message to push; must not be NULL
 */
void SkalQueuePush(SkalQueue* queue, SkalMsg* msg);


/** Pop a message from the queue
 *
 * This is a blocking function! Actually, this is the only blocking function in
 * the whole SKAL framework! (There are blocking functions in skal-net, but they
 * should not block if used carefully). If the queue is not empty, this function
 * will pop the message currently at the front of the queue. If the queue is
 * empty, this function will block until a message is pushed into it.
 *
 * If the `internalOnly` argument is set, urgent and regular messages are
 * ignored, and only internal messages are popped. If no internal message is
 * available, the function blocks, regardless of whether urgent or regular
 * messages are available.
 *
 * Messages are popped in the following order:
 *  - Internal messages first
 *  - If there are no internal message pending, urgent messages
 *  - Otherwise, regular messages
 *
 * @param queue        [in,out] From where to pop a message
 * @param internalOnly [in]     Whether to wait for internal messages only
 *
 * @return The popped message; this function never returns NULL
 */
SkalMsg* SkalQueuePop_BLOCKING(SkalQueue* queue, bool internalOnly);


/** Pop a message from the queue, non-blocking version
 *
 * This function works like `SkalQueuePop_BLOCKING()` except that it does not
 * block and return NULL if the queue is empty.
 *
 * @param queue        [in,out] From where to pop a message
 * @param internalOnly [in]     Whether to wait for internal messages only
 *
 * @return The popped message, or NULL if queue is empty
 */
SkalMsg* SkalQueuePop(SkalQueue* queue, bool internalOnly);


/** Check if the queue is full (or more than full) */
bool SkalQueueIsOverHighThreshold(const SkalQueue* queue);


/** Check if the queue is half-full (or more than half-full) */
bool SkalQueueIsOverLowThreshold(const SkalQueue* queue);



#ifdef __cplusplus
}
#endif

#endif /* SKAL_QUEUE_h_ */
