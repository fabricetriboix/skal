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

#ifndef SKAL_h_
#define SKAL_h_

/** SKAL
 *
 * \defgroup skal SKAL
 * \addtogroup skal
 * @{
 *
 * Please note all strings must be ASCII strings of at most `SKAL_NAME_MAX` in
 * size (including the terminating null character), unless otherwise noted.
 * Additional constraints may be imposed, in which case they will be clearly
 * mentioned in comments. Please note strict checks are perfomed on all strings.
 */

#include "skalcommon.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Message flag: it's OK to receive this message out of order
 *
 * Skal will send this message using a link that does not necessarily keep
 * packets ordering, in exchange of a faster transfer.
 *
 * The default is to send data over a reliable transport link that reliability
 * deliver packets in the same order they are sent (such as TCP).
 */
#define SKAL_MSG_FLAG_OUT_OF_ORDER_OK 0x01


/** Message flag: it's OK to drop this message
 *
 * Skal will send this message using a link that might drop packets, in
 * exchange of a faster transfer.
 *
 * The default is to send data over a reliable transport link that reliability
 * deliver packets in the same order they are sent (such as TCP).
 */
#define SKAL_MSG_FLAG_DROP_OK 0x02


/** Message flag: send the message over a UDP-like link
 *
 * Skal will send this message using a UDP-like link, i.e. is a combination of
 * the two previous flags.
 *
 * The default is to send data over a reliable transport link that reliability
 * deliver packets in the same order they are sent (such as TCP).
 */
#define SKAL_MSG_FLAG_UDP (SKAL_MSG_FLAG_OUT_OF_ORDER_OK|SKAL_MSG_FLAG_DROP_OK)


/** Message flag: notify sender of dropped packet
 *
 * A "skal-msg-drop" message will be sent to the sender of this message if
 * the message is dropped before reaching its destination. This flag has no
 * effect unless the `SKAL_MSG_FLAG_DROP_OK` flag is also set.
 */
#define SKAL_MSG_FLAG_NTF_DROP 0x04


/** Message flag: this message is urgent
 *
 * This message will jump in front of non-urgent messages in most queues.
 *
 * **WARNING**: Use this flag sparringly; it should normally never be used on
 * regular data (especially high throughput data).
 */
#define SKAL_MSG_FLAG_URGENT 0x08


/** Prototype for a custom allocator
 *
 * Such a function will be called to allocate a custom memory area.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `id`: Optional identifier (eg: buffer slot on a video card)
 *  - `size_B`: Optional minimum size of the memory area to allocate
 *
 * This function must return an object that will be used later to map and unmap
 * the memory area into the process/thread memory space. It must return NULL in
 * case of error.
 *
 * **This function is not allowed to block!**
 */
typedef void* (*SkalAllocateF)(void* cookie, const char* id, int64_t size_B);


/** Prototype for a custom de-allocator
 *
 * This function will be called to de-allocate a memory area previously
 * allocated with `SkalAllocateF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: Object pointer returned by `SkalAllocateF` which must now be
 *    de-allocated
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalFreeF)(void* cookie, void* obj);


/** Prototype to map a custom memory area
 *
 * This function will be called to allow the current process/thread to read
 * and/or write a memory area previously allocated with `SkalAllocateF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: Object pointer returned by `SkalAllocateF` which must now be mapped
 *    into the process memory space
 *
 * This function must return a pointer to the mapped memory area, accessible
 * from the current process, or NULL in case of error.
 *
 * **This function is not allowed to block!**
 */
typedef void* (*SkalMapF)(void* cookie, void* obj);


/** Prototype to unmap a custom memory area
 *
 * This function will be called to terminate the current mapping initiated by a
 * previous call to `SkalMapF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: Object pointer returned by `SkalAllocateF` which must now be
 *    unmapped from the process memory space
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalUnmapF)(void* cookie, void* obj);


/** The different scopes of a custom memory allocator
 *
 * Whenever a blob will move to a wider scope (eg: thread -> process), a new
 * blob of the larger scope will be allocated, the content of the old blob will
 * be copied to the new blob, and the old blob will be deleted and replaced by
 * the new blob.
 */
typedef enum
{
    /** The scope is limited to the current thread */
    SKAL_ALLOCATOR_SCOPE_THREAD,

    /** The scope is limited to the current process; eg: "malloc" allocator */
    SKAL_ALLOCATOR_SCOPE_PROCESS,

    /** The scope is the current machine; eg: "shm" allocator */
    SKAL_ALLOCATOR_SCOPE_COMPUTER,

    /** The scope is the current system; eg: a NAS-backed object */
    SKAL_ALLOCATOR_SCOPE_SYSTEM
} SkalAllocatorScope;


/** Structure representing a custom memory allocator
 *
 * This could be used, for example, to allocate frame buffers on a video card,
 * network packets from a network processor, and other such exotic memory areas
 * that do not belong to the computer RAM. You could use it for a fixed-size
 * buffer pool if you fancy trying to get smarter than the excellent glibc
 * malloc.
 *
 * Doing so might help avoiding memory copies.
 *
 * Please note that SKAL already provides the following allocators:
 *  - "malloc": Allocates memory accessible within the process (this uses
 *    `malloc` to do so)
 *  - "shm": Allocates memory accessible within the computer (this uses the
 *    operating system shared memory capabilities)
 */
typedef struct
{
    /** Allocator name
     *
     * This must be unique within the allocator's scope.
     */
    const char name[SKAL_NAME_MAX];

    /** Allocator scope
     *
     * Please refer to `SkalAllocatorScope` for more details.
     */
    SkalAllocatorScope scope;

    /** Allocate a memory area; NULL not allowed */
    SkalAllocateF allocate;

    /** Free a memory area; NULL not allowed */
    SkalFreeF free;

    /** Map a memory area into the process memory space; NULL not allowed */
    SkalMapF map;

    /** Unmap a memory area from the process memory space; NULL not allowed */
    SkalUnmapF unmap;

    /** Cookie for the previous functions */
    void* cookie;
} SkalAllocator;


/** Opaque type to a blob
 *
 * A blob is an arbitrary chunk of binary data. A blob is usually "large", from
 * a few KiB to a few GiB.
 */
typedef struct SkalBlob SkalBlob;


/** Opaque type to a SKAL message
 *
 * Messages are the basic building block of SKAL. Messages carry information and
 * data, and trigger actions executed by threads.
 */
typedef struct SkalMsg SkalMsg;


/** Prototype of a function that processes a message
 *
 * The arguments are:
 *  - `cookie`: Same as `SkalThreadCfg.cookie`
 *  - `msg`: Message that triggered this call; ownership of `msg` is transferred
 *    to you, it is up to you to free it when you're finished with it, or send
 *    it to another thread.
 *
 * If you want to terminate the thread, this function should return `false` and
 * you wish will be executed with immediate effect. Otherwise, just return
 * `true`.
 *
 * **This function is not allowed to block!**
 */
typedef bool (*SkalProcessMsgF)(void* cookie, SkalMsg* msg);


/** Structure representing a thread */
typedef struct
{
    /** Thread name
     *
     * This must be unique within this process. This must not be an empty
     * string.
     */
    char name[SKAL_NAME_MAX];

    /** Message processing function; must not be NULL */
    SkalProcessMsgF processMsg;

    /** Cookie for the previous function */
    void* cookie;

    /** Message queue threshold for this thread; use 0 for default
     *
     * Messages will be processed as fast as possible, but if some backing up
     * occurs, they will be queued there. If the number of queued messages
     * reached this threshold, throttling or message drops will occur.
     */
    int64_t queueThreshold;

    /** Size of the stack for this thread; if <= 0, use OS default */
    int32_t stackSize_B;

    /** TODO */
    int statsCount;
} SkalThreadCfg;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise SKAL for this process
 *
 * A skald daemon should be running on the same computer before this call is
 * made.
 *
 * \param skaldUrl   [in] URL to connect to skald. This may be NULL. Actually,
 *                        this should be NULL unless you really know what you
 *                        are doing.
 * \param allocators [in] A NULL-teminated array of custom blob allocators. This
 *                        may be NULL. Actually, this should be NULL, unless you
 *                        have some special way to store data (like a video
 *                        card storing frame buffers).
 *
 * If allocator names are not unique, the latest one will be used. This could be
 * used to override the pre-defined "malloc" and "shm" allocators, although this
 * is discouraged.
 *
 * Please note that if an allocator has a scope of
 * `SKAL_ALLOCATOR_SCOPE_COMPUTER` or `SKAL_ALLOCATOR_SCOPE_SYSTEM`, you will
 * have to ensure that this allocator is also registered by any process that
 * might free blobs created by this allocator. Failure to do so will result in
 * asserts.
 *
 * \return `true` if OK, `false` if can't connect to skald
 */
bool SkalInit(const char* skaldUrl, const SkalAllocator* allocators);


/** Terminate this process
 *
 * This function can typically be called from a signal handler.
 */
void SkalExit(void);


/** Create a thread
 *
 * NB: The only way to terminate a thread is for its `processMsg` callback to
 * return `false`, or for the process itself to be terminated.
 *
 * \param thread [in] Description of the thread to create
 */
void SkalThreadCreate(const SkalThreadCfg* cfg);


/** Subscribe the current thread to the given group
 *
 * This function must be called from within the thread that must subscribe.
 *
 * \param group [in] Group to subscribe to; must not be NULL
 */
void SkalThreadSubscribe(const char* group);


/** Unsubscribe the current thread from the given group
 *
 * This function must be called from within the thread that must unsubscribe.
 *
 * \param group [in] Group to unsubscribe to; must not be NULL
 */
void SkalThreadUnsubscribe(const char* group);


/** SKAL main loop
 *
 * This should be your last call in your `main()` function. This function does
 * not return.
 */
void SkalLoop(void) __attribute__((noreturn));


/** Create a blob
 *
 * \param allocator [in] Allocator to use to create the blob. This may be NULL,
 *                       or the empty string, in which case the "malloc"
 *                       allocator is used.
 * \param id        [in] Identifier for the allocator. NULL may or may not be a
 *                       valid value depending on the allocator.
 * \param name      [in] A name for this blob; may be NULL.
 * \param size_B    [in] Minimum number of bytes to allocate. <= 0 may or
 *                       may not be allowed, depending on the allocator.
 *
 * The following allocators are always available:
 *  - "malloc" (which is used when the `allocator` argument is NULL): This
 *    allocates memory using `malloc()`. The `id` argument is ignored, and
 *    `size_B` must be > 0.
 *  - "shm": This allocates shared memory using `shm_open()`, etc. This type of
 *    memory can be accessed by various processes within the same computer. If
 *    you know you are creating a blob that will be sent to another process,
 *    creating it using the "shm" allocator will gain a bit of time because it
 *    will avoid an extra step of converting a "malloc" blob to a "shm" blob.
 *    The `id` argument is ignored, and `size_B` must be > 0.
 *
 * Please note that if you create a blob using the "malloc" allocator, and
 * subsequently send the blob to another process, that's perfectly fine. Your
 * "malloc" blob will be automatically changed into a "shm" blob. This also
 * applies to a blob created with a custom allocator with the
 * `SkalAllocator.interProcess` flag set to `false`.
 *
 * **VERY IMPORTANT** SKAL does not provide any mechanism to allow exclusive
 * access to a blob (no mutex, no semaphore, etc.) as this would go against SKAL
 * philosophy. It is expected that the application designer will be sensible in
 * how the application will be designed.
 *
 * Let's expand a bit on the previous remark. First of all, please note there
 * are 2 ways a blob can be sent to more than one thread:
 *  - A blob is referenced by different messages, and the messages are sent to
 *    their respective threads
 *  - A blob is referenced by only one message, and the message is sent to a
 *    group which has 2 or more subscribers
 *
 * The following use cases will not require any special attention:
 *  - The blob is sent to only one thread
 *  - The blob is sent to 2 or more threads which will not modify the memory
 *    area pointed to by the blob
 *
 * The following use case will require special attention: the blob is sent to
 * more than one thread, one of them will write to the memory area pointed to by
 * the blob. You would then have a race condition between the writing thread and
 * the reading thread(s). This situation would essentially show a bad
 * application design. Either:
 *  - the data needs to be modified before it is read; in which case the writing
 *    thread should by the only recipient and forward the blob after
 *    modification
 *  - it does not matter whether the data is modified before or after it is
 *    read; the same as above applies here
 *  - the data needs to be modified after it is read; in this case, a more
 *    complex mechanism, possibly based on reference counters, would be required
 *
 * \return The newly created blob with its reference count set to 1, or NULL in
 *         case of error (`allocator` does not exist, invalid `id` or `size_B`
 *         for the chosen allocator, or failed to allocate)
 */
SkalBlob* SkalBlobCreate(const char* allocator, const char* id,
        const char* name, int64_t size_B);


/** Add a reference to a blob
 *
 * This will increment the blob's reference counter by one.
 *
 * \param blob [in,out] Blob to reference; must not be NULL
 */
void SkalBlobRef(SkalBlob* blob);


/** Remove a reference to a blob
 *
 * This will decrement the blob's reference counter by one.
 *
 * If the reference counter becomes 0, the blob is freed. Therefore, always
 * assume you are the last to call `SkalBlobUnref()` on that blob and that it
 * does not exist anymore when this function returns.
 *
 * \param blob [in,out] Blob to de-reference; must not be NULL
 */
void SkalBlobUnref(SkalBlob* blob);


/** Map the blob's memory into the current process memory
 *
 * You have to map the blob in order to read/write the memory area it
 * represents.
 *
 * *You do not have exclusive access to the blob*, please refer to
 * `SkalBlobCreate()` for more information.
 *
 * \param blob [in,out] Blob to map; must not be NULL
 *
 * \return A pointer to the mapped memory area, or NULL in case of error
 */
void* SkalBlobMap(SkalBlob* blob);


/** Unmap the blob's memory from the current process memory
 *
 * Always call this function as soon as possible after a call to
 * `SkalBlobMap()`.
 *
 * \param blob [in,out] Blob to unmap; must not be NULL
 */
void SkalBlobUnmap(SkalBlob* blob);


/** Get the blob's id
 *
 * \param blob [in] Blob to query; must not be NULL
 *
 * \return The blob id, which may be NULL
 */
const char* SkalBlobId(const SkalBlob* blob);


/** Get the blob's name
 *
 * \param blob [in] Blob to query; must not be NULL
 *
 * \return The blob name, which may be NULL
 */
const char* SkalBlobName(const SkalBlob* blob);


/** Get the blob's size, in bytes
 *
 * \param blob [in] The blob to query; must not be NULL
 *
 * \return The blob's size, in bytes, which may be 0
 */
int64_t SkalBlobSize_B(const SkalBlob* blob);


/** Create an empty message
 *
 * \param type   [in] Message's type. This argument may not be NULL and may
 *                    not be an empty string. Please note that message types
 *                    starting with "skal-" are reserved for SKAL's own use, so
 *                    please avoid prefixing your message types with "skal-".
 * \param flags  [in] Message flags; please refer to `SKAL_MSG_FLAG_*`
 * \param marker [in] A marker that helps uniquely identify this message. This
 *                    argument may be NULL, in which case a marker will be
 *                    automatically generated.
 *
 * \return The newly created SKAL message; this function never returns NULL
 */
SkalMsg* SkalMsgCreate(const char* type, const char* recipient,
        uint8_t flags, const char* marker);


/** Remove a reference from a message
 *
 * This will decrement the message reference counter by one. If blobs are
 * attached to the message, their reference counters are also decremented.
 *
 * If the message reference counter reaches zero, the message is freed.
 * Therefore, always assumes you are the last one to call `SkalMsgUnref()` and
 * that the message has been freed and is no longer available when this function
 * returns.
 *
 * \param msg [in,out] Message to de-reference; must not be NULL; might be
 *                     freed by this call
 */
void SkalMsgUnref(SkalMsg* msg);


/** Get the message type
 *
 * \param msg [in] Message to query; must not be NULL
 *
 * \return The message type; never NULL
 */
const char* SkalMsgType(const SkalMsg* msg);


/** Get the message sender
 *
 * \param msg [in] Message to query; must not be NULL
 *
 * \return The message sender; never NULL
 */
const char* SkalMsgSender(const SkalMsg* msg);


/** Get the message recipient
 *
 * \param msg [in] Message to query; must not be NULL
 *
 * \return The message recipient; never NULL
 */
const char* SkalMsgRecipient(const SkalMsg* msg);


/** Get the message flags
 *
 * \param msg [in] Message to query; must not be NULL
 *
 * \return The message flags
 */
uint8_t SkalMsgFlags(const SkalMsg* msg);


/** Get the message marker
 *
 * \param msg [in] Message to query; must not be NULL
 *
 * \return The message marker, never NULL
 */
const char* SkalMsgMarker(const SkalMsg* msg);


/** Add an extra integer to the message
 *
 * \param msg  [in,out] Message to manipulate; must not be NULL
 * \param name [in]     Name of the integer; must not be NULL
 * \param i    [in]     Integer to add
 */
void SkalMsgAddInt(SkalMsg* msg, const char* name, int64_t i);


/** Add an extra floating-point number to the message
 *
 * \param msg  [in,out] Message to manipulate; must not be NULL
 * \param name [in]     Name of the double; must not be NULL
 * \param d    [in]     Double to add
 */
void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d);


/** Add an extra string to the message
 *
 * \param msg  [in,out] Message to manipulate; must not be NULL
 * \param name [in]     Name of the string; must not be NULL
 * \param s    [in]     String to add; must not be NULL; must be UTF-8 encoded
 *                      and null-terminated; can be of arbitrary length.
 */
void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s);


/** Add an extra binary field to the message
 *
 * Unlike a blob, this field will be copied every time a message moves from one
 * process to another. So this would be suitable from small data (a few kiB at
 * most).
 *
 * \param msg    [in,out] Message to manipulate; must not be NULL
 * \param name   [in]     Name of the string; must not be NULL
 * \param data   [in]     Data to add; must not be NULL
 * \param size_B [in]     Number of bytes to add; must be > 0
 */
void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const void* data, int size_B);


/** Attach a blob to a message
 *
 * Please note the ownership of the blob is transferred to the `msg`. If you
 * want to continue accessing the blob after this call, you need to take a
 * reference first, by calling `SkalBlobRef(blob)`.
 *
 * \param msg  [in,out] Message to modify; must not be NULL
 * \param name [in]     Name of the blob; must not be NULL
 * \param blob [in]     Blob to attach; must not be NULL
 */
void SkalMsgAttachBlob(SkalMsg* msg, const char* name, SkalBlob* blob);


/** Get the value of an integer previously added to a message
 *
 * \param msg  [in] Message to query; must not be NULL
 * \param name [in] Name of the integer; must exists in this `msg`
 *
 * \return The value of the integer
 */
int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name);


/** Get the value of a double previously added to a message
 *
 * \param msg  [in] Message to query; must not be NULL
 * \param name [in] Name of the double; must exists in this `msg`
 *
 * \return The value of the double
 */
double SkalMsgGetDouble(const SkalMsg* msg, const char* name);


/** Get the value of a string previously added to a message
 *
 * \param msg  [in] Message to query; must not be NULL
 * \param name [in] Name of the string; must exists in this `msg`
 *
 * \return The value of the string; this function never returns NULL
 */
const char* SkalMsgGetString(const SkalMsg* msg, const char* name);


/** Get the value of a binary field previously added to a message
 *
 * If the supplied `buffer` is too small, the binary data is truncated to fit
 * inside `buffer`. In any case, this function returns the size of the binary
 * data, which might be greater than the size of `buffer`, if it was too small.
 *
 * \param msg    [in]  Message to query; must not be NULL
 * \param name   [in]  Name of the binary field; must exists in this `msg`
 * \param buffer [out] Where to write the binary field value; must not be NULL
 * \param size_B [in]  Size of `buffer`, in bytes; must be > 0
 *
 * \return The number of bytes of the binary field, which might be > `size_B`
 */
int SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        void* buffer, int size_B);


/** Access a blob from a message
 *
 * The blob's reference counter will be incremented for you. Please call
 * `SkalBlobUnref(blob)` when you are done with it.
 *
 * \param msg  [in] Message to query; must not be NULL
 * \param name [in] Name of the blob; must exists in this `msg`
 *
 * \return The found blob; this function never returns NULL
 */
SkalBlob* SkalMsgGetBlob(const SkalMsg* msg, const char* name);


/** Detach a blob from a message
 *
 * The ownership of the found blob will be transferred to you.
 *
 * \param msg  [in,out] Message to manipulate; must not be NULL
 * \param name [in]     Name of the blob; must exists in this `msg`
 *
 * \return The found blob; this function never returns NULL
 */
SkalBlob* SkalMsgDetachBlob(SkalMsg* msg, const char* name);


/** Make a copy of a message
 *
 * \param msg       [in] Message to copy
 * \param refBlobs  [in] If set to `false`, the new message will not have any
 *                       blob. If set to `true`, the new message will reference
 *                       all the blobs attached to `msg`
 * \param recipient [in] Recipient for this new message; may be NULL to keep the
 *                       same recipient as `msg`
 *
 * \return The copied message; this function never returns NULL
 */
SkalMsg* SkalMsgCopy(const SkalMsg* msg, bool refBlobs, const char* recipient);


/** Send a message to its recipient
 *
 * You will lose ownership of the message.
 *
 * \param msg [in,out] Message to send
 */
void SkalMsgSend(SkalMsg* msg);



/* @} */
#endif /* SKAL_h_ */
