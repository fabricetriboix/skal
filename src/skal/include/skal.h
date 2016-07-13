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
 */

#include "skalcommon.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Maximum number of custom allocators */
#define SKAL_MAX_ALLOCATORS 500


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
 */
typedef void* (*SkalAllocateF)(void* cookie, const char* id, int64_t size_B);


/** Prototype for a custom de-allocator
 *
 * This function will be called to de-allocate a memory area previously
 * allocated with `SkalAllocateF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: The object pointer returned by `SkalAllocateF` which must now
 *           be de-allocated
 */
typedef void (*SkalFreeF)(void* cookie, void* obj);


/** Prototype to map a custom memory area
 *
 * This function will be called to allow the current process/thread to read
 * and/or write a memory area previously allocated with `SkalAllocateF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: The object pointer returned by `SkalAllocateF` which must now
 *           be mapped into the process memory space
 *
 * This function must return a pointer to the mapped memory area, accessible
 * from the current process, or NULL in case of error.
 */
typedef void* (*SkalMapF)(void* cookie, void* obj);


/** Prototype to unmap a custom memory area
 *
 * This function will be called to terminate the current mapping initiated by a
 * previous call to `SkalMapF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `obj`: The object pointer returned by `SkalAllocateF` which must now
 *           be unmapped from the process memory space
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
     * This must be a valid UTF-8 string and be unique within its scope.
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
 *  - `cookie`: The same as `SkalThread.cookie`
 *  - `msg`: The message that triggered this call; the ownership of `msg` is
 *    transferred to you, it is up to you to free when you're finished with it,
 *    or send it to another thread.
 *
 * If you want to terminate the thread, this function should return `false` and
 * you wish will be executed with immediate effect. Otherwise, just return
 * `true`.
 */
typedef bool (*SkalProcessMsgF)(void* cookie, SkalMsg* msg);


/** Structure representing a thread */
typedef struct
{
    /** Thread name
     *
     * This must be valid ASCII string and be unique within this process.
     */
    char name[SKAL_NAME_MAX];

    /** Message processing function; must not be NULL */
    SkalProcessMsgF processMsg;

    /** Cookie for the previous function */
    void* cookie;

    /** Message queue capacity for this thread; must be > 0
     *
     * Messages will be processed as fast as possible, but if some backing up
     * occurs, they will be queued there.
     */
    int queueCapacity;

    /** Size of the stack for this thread; if <= 0, use OS default */
    int stackSize_B;

    /** NULL-terminated list of groups to subscribe this thread to initially */
    char** groups;

    /** TODO */
    int statsCount;
} SkalThread;



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


/** Create a thread
 *
 * \param thread [in] Description of the thread to create
 */
void SkalThreadCreate(const SkalThread* thread);


/** Subscribe the current thread to the given group
 *
 * This function must be called from within the thread that must subscribe.
 *
 * \param group [in] Group to subscribe to
 */
void SkalThreadSubscribe(const char* group);


/** Unsubscribe the current thread from the given group
 *
 * This function must be called from within the thread that must unsubscribe.
 *
 * \param group [in] Group to unsubscribe to
 */
void SkalThreadUnsubscribe(const char* group);


/** TODO */
void SkalTimerCreate(/* TODO */);


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
 * \param name      [in] A name for this blob; may be NULL, but if not NULL it
 *                       must be a valid UTF-8 string at most `SKAL_NAME_MAX` in
 *                       size, including the terminating null character
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
 * \param type   [in] The message's type. This argument may not be NULL and must
 *                    be an ASCII string of at least 1 and at most
 *                    `SKAL_NAME_MAX - 1` characters. Please note that message
 *                    types starting with "SKAL-" are reserved for SKAL's own
 *                    use.
 * \param marker [in] A marker that helps uniquely identify this message. This
 *                    argument may be NULL, in which case a marker will be
 *                    automatically generated. If not NULL, this string must be
 *                    at most `SKAL_NAME_MAX - 1` characters in size.
 *
 * \return The newly created SKAL message, with its reference counter set to 1.
 *         This function never returns NULL.
 */
SkalMsg* SkalMsgCreate(const char* type, const char* marker);


/** Add a reference to a message
 *
 * This will increment the message reference counter by one. If blobs are
 * attached to the message, their reference counters are also incremented.
 *
 * \param msg [in,out] The message to reference; must not be NULL
 */
void SkalMsgRef(SkalMsg* msg);


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
 * \param msg [in,out] The message to de-reference; must not be NULL; might be
 *                     freed by this call
 */
void SkalMsgUnref(SkalMsg* msg);


/** Add an extra integer to the message
 *
 * \param msg  [in,out] The message to manipulate; must not be NULL
 * \param name [in]     Name of the integer; must not be NULL; must be an
 *                      ASCII string at most `SKAL_NAME_MAX - 1` in size; this
 *                      field name must be unique for this message.
 * \param i    [in]     The integer to add
 */
void SkalMsgAddInt(SkalMsg* msg, const char* name, int64_t i);


/** Add an extra floating-point number to the message
 *
 * \param msg  [in,out] The message to manipulate; must not be NULL
 * \param name [in]     Name of the double; must not be NULL; must be an
 *                      ASCII string at most `SKAL_NAME_MAX - 1` in size; this
 *                      field name must be unique for this message.
 * \param d    [in]     The double to add
 */
void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d);


/** Add an extra string to the message
 *
 * \param msg  [in,out] The message to manipulate; must not be NULL
 * \param name [in]     Name of the string; must not be NULL; must be an
 *                      ASCII string at most `SKAL_NAME_MAX - 1` in size; this
 *                      field name must be unique for this message.
 * \param s    [in]     The string to add; must not be NULL; must be UTF-8
 *                      encoded; can be of arbitrary length.
 */
void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s);


/** Add an extra binary field to the message
 *
 * Unlike a blob, this field will be copied every time a message moves from one
 * process to another. So this would be suitable from small data (a few kiB at
 * most).
 *
 * \param msg    [in,out] The message to manipulate; must not be NULL
 * \param name   [in]     Name of the string; must not be NULL; must be an
 *                        ASCII string at most `SKAL_NAME_MAX - 1` in size; this
 *                        field name must be unique for this message.
 * \param data   [in]     The data to add; must not be NULL
 * \param size_B [in]     The number of bytes to add; must be > 0
 */
void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const void* data, int size_B);


/** Get the message type
 *
 * \param msg [in] The message to query; must not be NULL
 *
 * \return The message type, never NULL
 */
const char* SkalMsgType(const SkalMsg* msg);


/** Get the message marker
 *
 * \param msg [in] The message to query; must not be NULL
 *
 * \return The message marker, never NULL
 */
const char* SkalMsgMarker(const SkalMsg* msg);


/** Get the value of an integer previously added to a message
 *
 * \param msg  [in] The message to query; must not be NULL
 * \param name [in] The name of the integer; must exists in this `msg`
 *
 * \return The value of the integer
 */
int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name);


/** Get the value of a double previously added to a message
 *
 * \param msg  [in] The message to query; must not be NULL
 * \param name [in] The name of the double; must exists in this `msg`
 *
 * \return The value of the double
 */
double SkalMsgGetDouble(const SkalMsg* msg, const char* name);


/** Get the value of a string previously added to a message
 *
 * \param msg  [in] The message to query; must not be NULL
 * \param name [in] The name of the string; must exists in this `msg`
 *
 * \return The value of the string
 */
const char* SkalMsgGetString(const SkalMsg* msg, const char* name);


/** Get the value of a binary filed previously added to a message
 *
 * If the supplied `buffer` is too small, the binary data is truncated to fit
 * inside `buffer`. In any case, this function returns the size of the binary
 * data, which might be greater than the size of `buffer`, if it was too small.
 *
 * \param msg [in] The message to query; must not be NULL
 * \param name [in] The name of the binary field; must exists in this `msg`
 * \param buffer [out] Where to write the binary field value; must not be NULL
 * \param size_B [in]  Size of `buffer`, in bytes; must be > 0
 *
 * \return The number of bytes of the binary field, which might be > `size_B`
 */
int SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        void* buffer, int size_B);


/** Attach a blob to a message
 *
 * Please note the ownership of the blob is transferred to the `msg`. If you
 * want to continue accessing the blob after this call, you need to take a
 * reference first, by calling `SkalBlobRef(blob)`.
 *
 * \param msg  [in,out] The message to modify; must not be NULL
 * \param blob [in]     The blob to attach; must not be NULL
 */
void SkalMsgAttachBlob(SkalMsg* msg, SkalBlob* blob);


/** Get the number of blobs attached to a message
 *
 * \param msg [in] The message to query; must not be NULL
 *
 * \return The number of blobs attached to `msg`
 */
int SkalMsgNBlob(const SkalMsg* msg);


/** Detach the first blob from a message
 *
 * The ownership of the blob will be transferred to you.
 *
 * Please note this function will, if successful, decrement the number of blobs
 * attached to the message by one.
 *
 * \param msg [in,out] The message to manipulate; must not be NULL
 *
 * \return The first blob, or NULL if no more blobs in this `msg`
 */
SkalBlob* SkalMsgPopBlob(SkalMsg* msg);


/** Detach a blob from a message, given its index
 *
 * The ownership of the found blob will be transferred to you.
 *
 * Please note this function will, if successful, decrement the number of blobs
 * attached to the message by one.
 *
 * \param msg   [in,out] The message to manipulate; must not be NULL
 * \param index [in]     Index of the blob, which is between 0 and
 *                       `SkalMsgNBlob(msg) - 1`
 *
 * \return The found blob, or NULL if `index` is out of range
 */
SkalBlob* SkalMsgDetachBlob(SkalMsg* msg, int index);


/** Detach a blob from a message, given its id
 *
 * The ownership of the found blob will be transferred to you.
 *
 * Please note this function will, if successful, decrement the number of blobs
 * attached to the message by one.
 *
 * \param msg [in,out] The message to manipulate; must not be NULL
 * \param id  [in]     Id of the blob; must not be NULL
 *
 * \return The found blob, or NULL if not found
 */
SkalBlob* SkalMsgDetachBlobById(SkalMsg* msg, const char* blobId);


/** Access a blob from a message, given its index
 *
 * The ownership of the found blob will not be transferred to you, but its
 * reference counter will be incremented for you. Please call
 * `SkalBlobUnref(blob)` when you are done with it.
 *
 * \param msg   [in] The message to query; must not be NULL
 * \param index [in] Index of the blob, which is between 0 and
 *                   `SkalMsgNBlob(msg) - 1`
 *
 * \return The found blob, or NULL if `index` is out of range
 */
SkalBlob* SkalMsgGetBlob(const SkalMsg* msg, int index);


/** Access a blob from a message, given its id
 *
 * The ownership of the found blob will not be transferred to you, but its
 * reference counter will be incremented for you. Please call
 * `SkalBlobUnref(blob)` when you are done with it.
 *
 * \param msg [in] The message to query; must not be NULL
 * \param id  [in] Id of the blob; must not be NULL
 *
 * \return The found blob, or NULL if `index` is out of range
 */
SkalBlob* SkalMsgGetBlobById(const SkalMsg* msg, const char* blobId);


/** Make a copy of a message
 *
 * \param msg      [in] The message to copy
 * \param refBlobs [in] If set to `false`, the new message will not have any
 *                      blob. If set to `true`, the new message will reference
 *                      all the blobs attached to `msg`
 *
 * \return The copied message. This function never returns NULL.
 */
SkalMsg* SkalMsgCopy(const SkalMsg* msg, bool refBlobs);


/** Send a message
 *
 * You will lose the ownership of the message; you must assume `msg` does not
 * exist anymore when this function returns.
 *
 * When sending a message to another thread of the same process, and if the
 * recipient's queue is full, your thread will be throttled. This means it will
 * not process any message until is has been able to send this `msg`. If you
 * don't like this, please use the `SkalMsgSendOrFail()` function.
 *
 * \param msg [in] The message to send
 * \param to  [in] Name of the recipient (can be a thread or a group)
 */
void SkalMsgSend(SkalMsg* msg, const char* to);


/** Send a message (without throttling)
 *
 * If this function succeeds (that is: returns `true`), you have lost the
 * ownership of the message; you must then assume `msg` does not exist anymore
 * when this function returns.
 *
 * \param msg [in] The message to send
 * \param to  [in] Name of the recipient (can be a thread or a group)
 *
 * \return `true` if the message has been sent, `false` if the recipient's queue
 *         is full
 */
bool SkalMsgSendOrFail(SkalMsg* msg, const char* to);



/* @} */
#endif /* SKAL_h_ */
