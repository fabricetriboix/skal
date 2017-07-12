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

#ifndef SKAL_h_
#define SKAL_h_

#ifdef __cplusplus
extern "C" {
#endif

/** SKAL
 *
 * @defgroup skal SKAL
 * @addtogroup skal
 * @{
 *
 * Please note all strings must be ASCII strings unless otherwise noted.
 * Additional constraints may be imposed, in which case they will be clearly
 * mentioned in comments.
 */

#include "skal-common.h"
#include "cdsmap.h"
#include "cdslist.h"



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


/** Message flag: it's OK to silently drop this message
 *
 * Skal will send this message using a link that might drop packets, in
 * exchange of a faster transfer.
 *
 * The default is to send data over a reliable transport link that reliability
 * deliver packets in the same order they are sent (such as TCP).
 */
#define SKAL_MSG_FLAG_DROP_OK 0x02


/** Message flag: send this message over a UDP-like link
 *
 * Skal will send this message using a UDP-like link, i.e. is a combination of
 * the two previous flags.
 *
 * The default is to send data over a reliable transport link that reliability
 * deliver packets in the same order they are sent (such as TCP).
 */
#define SKAL_MSG_FLAG_UDP (SKAL_MSG_FLAG_OUT_OF_ORDER_OK|SKAL_MSG_FLAG_DROP_OK)


/** Message flag: notify the sender if this packet is dropped
 *
 * A "skal-msg-drop" message will be sent to the sender of this message if
 * the message is dropped before reaching its destination. This flag has no
 * effect unless the `SKAL_MSG_FLAG_DROP_OK` flag is also set.
 */
#define SKAL_MSG_FLAG_NTF_DROP 0x04


/** Message flag: message is urgent
 *
 * This message will jump if front of regular messages in certain queues.
 */
#define SKAL_MSG_FLAG_URGENT 0x08


/** Message flag: this is a multicast message */
#define SKAL_MSG_FLAG_MULTICAST 0x10


// Forward declaration
struct SkalAllocator;


/** Structure representing a blob proxy
 *
 * A blob is an arbitrary chunk of binary data. A blob is usually "large", from
 * a few KiB to a few GiB.
 *
 * This structure is meant to be "derived" from and to provide access to the
 * underlying blob through the map/unmap functions.
 *
 * The lifetime of an object of type `SkalBlobProxy` is usually different (and
 * shorter) than the underlying blob. The blob proxy is only meant to access the
 * blob itself.
 *
 * Blob proxies can be items of `CdsList` lists.
 */
typedef struct {
    CdsListItem           item;      /**< Private! Do not touch! */
    struct SkalAllocator* allocator; /**< Allocator that allocated the blob */
} SkalBlobProxy;


/** Prototype of a function to create a blob
 *
 * Such a function will be called to create a blob. Your blob must implement a
 * concept of reference counter, and the reference counter must be set to 1 when
 * this function returns.
 *
 * This function must also create a blob proxy with a reference counter set to
 * 1. The reference counters to the blob and to the proxy are 2 different
 * things.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `id`: Identifier (eg: buffer slot on a video card, shared memory id, etc.)
 *  - `size_B`: Minimum size of the memory area to allocate, in bytes
 *
 * Whether you use the arguments or not is up to your allocator.
 *
 * This function must return a `SkalBlobProxy` object (or a structure derived
 * from it), which is a proxy to access the underlying blob. The blob proxy must
 * implement a reference counter which must be set to 1. You must initialise all
 * properties of the parent `SkalBlobProxy` structure to 0.
 *
 * This function must return NULL in case of error (in which case it should not
 * create any blob).
 *
 * **This function is not allowed to block!**
 */
typedef SkalBlobProxy* (*SkalCreateBlobF)(void* cookie,
        const char* id, int64_t size_B);


/** Prototype of a function to open an existing blob
 *
 * Such a function will be called to open an existing blob. The blob's reference
 * counter must be incremented by this function.
 *
 * This function must also create a blob proxy with a reference counter set to
 * 1. The reference counters to the blob and to the proxy are 2 different
 * things.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `id`: Identifier (eg: buffer slot on a video card, shared memory id, etc.)
 *
 * Whether you use the arguments or not is up to your allocator.
 *
 * This function must return a `SkalBlobProxy` object (or a structure derived
 * from it), which is a proxy to access the underlying blob. The blob proxy must
 * implement a reference counter which must be set to 1. You must initialise all
 * properties of the parent `SkalBlobProxy` structure to 0.
 *
 * This function must return NULL in case of error (in which case it should not
 * modify or reference any blob).
 *
 * **This function is not allowed to block!**
 */
typedef SkalBlobProxy* (*SkalOpenBlobF)(void* cookie, const char* id);


/** Prototype of a function to increment the reference counter of a blob proxy
 *
 * This should increment the reference counter of the proxy, not of the
 * underlying blob.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to reference
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalRefBlobProxyF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to decrement the reference counter of a blob proxy
 *
 * This should decrement the reference counter of the proxy. If the reference
 * counter reaches 0, this function should decrement the reference counter of
 * the underlying proxy (which may be de-allocated as a result), and de-allocate
 * the proxy itself.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to unreference
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalUnrefBlobProxyF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to increment the reference counter of a blob
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob to reference
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalRefBlobF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to decrement the reference counter of a blob
 *
 * If the reference counter reaches 0, this function must de-allocate the
 * underlying blob. It must not de-allocate the `proxy` object, though!
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob to unreference
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalUnrefBlobF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to map a blob
 *
 * This function will be called to allow the current process/thread to read
 * and/or write a the memory area pointed to by the blob.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob which must be mapped into the caller's memory
 *             space
 *
 * This function must return a pointer to the mapped memory area, accessible
 * from the current process, or NULL in case of error.
 *
 * After this function is called, you are guaranteed that only `SkalUnmapBlobF`
 * will be called on this blob, before any other `Skal*BlobF` functions.
 *
 * **This function is not allowed to block!**
 */
typedef uint8_t* (*SkalMapBlobF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to unmap a blob
 *
 * This function will be called to terminate the current mapping initiated by a
 * previous call to `SkalMapBlobF`.
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob which must now be unmapped from the process
 *             memory space
 *
 * **This function is not allowed to block!**
 */
typedef void (*SkalUnmapBlobF)(void* cookie, SkalBlobProxy* proxy);


/** Prototype of a function to get the blob's id
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob to query
 *
 * This function may return NULL if the blob does not have any id.
 *
 * **This function is not allowed to block**
 */
typedef const char* (*SkalBlobIdF)(void* cookie, const SkalBlobProxy* proxy);


/** Prototype of a function to get the blob's size in bytes
 *
 * The arguments are:
 *  - `cookie`: Same value as `SkalAllocator.cookie`
 *  - `proxy`: Proxy to the blob to query
 *
 * **This function is not allowed to block**
 */
typedef int64_t (*SkalBlobSizeF)(void* cookie, const SkalBlobProxy* proxy);


/** The different scopes of a custom memory allocator
 *
 * Whenever a blob will move to a wider scope (eg: thread -> process), a new
 * blob of the larger scope will be allocated, the content of the old blob will
 * be copied to the new blob, and the old blob will be deleted and replaced by
 * the new blob.
 */
typedef enum {
    /** The scope is limited to the current process; eg: "malloc" allocator */
    SKAL_ALLOCATOR_SCOPE_PROCESS,

    /** The scope is the current machine; eg: "shm" allocator */
    SKAL_ALLOCATOR_SCOPE_COMPUTER,

    /** The scope is the current system; eg: a NAS-backed object */
    SKAL_ALLOCATOR_SCOPE_SYSTEM
} SkalAllocatorScope;


/** Structure representing a blob allocator
 *
 * This could be used, for example, to allocate frame buffers on a video card,
 * network packets from a network processor, and other such exotic memory areas
 * that do not belong to the computer RAM. You could use it for a fixed-size
 * buffer pool if you fancy trying to get smarter than the excellent glibc
 * malloc.
 *
 * Doing so might help avoiding memory copies.
 *
 * Please note that skal already provides the following allocators:
 *  - "malloc": Allocates memory accessible within the process (this uses
 *    `malloc` to do so)
 *  - "shm": Allocates memory accessible within the computer (this uses the
 *    operating system shared memory capabilities)
 */
typedef struct SkalAllocator {
    CdsMapItem item; /**< Private, do not touch! */

    /** Allocator name
     *
     * This must be unique within the allocator's scope.
     */
    const char* name;

    /** Allocator scope
     *
     * Please refer to `SkalAllocatorScope` for more details.
     */
    SkalAllocatorScope scope;

    SkalCreateBlobF     create;     /**< Must not be NULL */
    SkalOpenBlobF       open;       /**< Must not be NULL */
    SkalRefBlobProxyF   refProxy;   /**< Must not be NULL */
    SkalUnrefBlobProxyF unrefProxy; /**< Must not be NULL */
    SkalRefBlobF        ref;        /**< Must not be NULL */
    SkalUnrefBlobF      unref;      /**< Must not be NULL */
    SkalMapBlobF        map;        /**< Must not be NULL */
    SkalUnmapBlobF      unmap;      /**< Must not be NULL */
    SkalBlobIdF         blobid;     /**< Must not be NULL */
    SkalBlobSizeF       blobsize;   /**< Must not be NULL */

    /** Cookie for the previous functions */
    void* cookie;
} SkalAllocator;


/** Opaque type to an alarm
 *
 * An alarm is information destined for the operator about an important
 * condition within the skal application.
 *
 * An alarm can either be on or off. It is normally turned on by skal when the
 * condition is detected. It can be turned off either by skal if it can
 * automatically detect that the condition is over, or it can be turned off by
 * the operator.
 *
 * Alarms can be pushed into and popped from `CdsList` lists.
 */
typedef struct SkalAlarm SkalAlarm;


/** Alarm severities */
typedef enum {
    SKAL_ALARM_NOTICE,
    SKAL_ALARM_WARNING,
    SKAL_ALARM_ERROR
} SkalAlarmSeverityE;


/** Opaque type to a skal message
 *
 * Messages are the basic building block of skal. Messages carry information and
 * data, and trigger actions executed by threads.
 */
typedef struct SkalMsg SkalMsg;


/** Prototype of a function that processes a message
 *
 * The arguments are:
 *  - `cookie`: Same as `SkalThreadCfg.cookie`
 *  - `msg`: Message that triggered this call; ownership of `msg` remains with
 *    the caller, do not call `SkalMsgUnref()` on that `msg`
 *
 * If you want to terminate the thread, this function should return `false` and
 * you wish will be executed with immediate effect. Otherwise, return `true` to
 * keep going.
 *
 * **This function is not allowed to block!**
 */
typedef bool (*SkalProcessMsgF)(void* cookie, SkalMsg* msg);


/** Structure representing a thread */
typedef struct {
    /** Thread name
     *
     * This must be unique within this process. This must not be an empty
     * string. It must not contain the character '@'.
     */
    const char* name;

    /** Message processing function; must not be NULL */
    SkalProcessMsgF processMsg;

    /** Cookie for the previous function */
    void* cookie;

    /** Message queue threshold for this thread; use 0 for default
     *
     * Messages will be processed as fast as possible, but if some backing up
     * occurs, they will be queued there. If the number of queued messages
     * reached this threshold, throttling will occur.
     */
    int64_t queueThreshold;

    /** Size of the stack for this thread; if <= 0, use OS default */
    int32_t stackSize_B;

    /** How long to wait before retrying to send; <=0 for default value
     *
     * If blocked by another thread for `xoffTimeout_us`, we will send that
     * thread a `skal-ntf-xon` to tell it to inform us whether we can send
     * again.
     */
    int64_t xoffTimeout_us;

    /** TODO */
    int statsCount;
} SkalThreadCfg;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Initialise skal for this process
 *
 * A skald daemon should be running on the same computer before this call is
 * made. You must call this function before any other function in this module.
 *
 * @param skaldUrl    [in] URL to connect to skald. This may be NULL. Actually,
 *                         this should be NULL unless you really know what you
 *                         are doing.
 * @param allocators  [in] Array of custom blob allocators. This may be NULL.
 *                         Actually, this should be NULL, unless you have some
 *                         special way to store data (like a video card storing
 *                         frame buffers).
 * @param nallocators [in] Number of allocators in the above array
 *
 * If allocator names are not unique, the latest one will be used. This could be
 * used to override the pre-defined "malloc" and "shm" allocators, although this
 * is discouraged.
 *
 * Please note that if an allocator has a scope of
 * `SKAL_ALLOCATOR_SCOPE_COMPUTER` or `SKAL_ALLOCATOR_SCOPE_SYSTEM`, you will
 * have to ensure that this allocator is also registered by any process that
 * might unreference blobs created by this allocator (because by definition this
 * blob may be de-allocated from another process). Failure to do so will result
 * in asserts.
 *
 * @return `true` if OK, `false` if can't connect to skald
 */
bool SkalInit(const char* skaldUrl,
        const SkalAllocator* allocators, int nallocators);


/** Terminate skal for this process
 *
 * This function terminates all threads managed by skal in this process and
 * de-allocates all resources used by skal.
 *
 * This function is blocking.
 */
void SkalExit(void);


/** Create a thread
 *
 * You must have called `SkalInit()` first.
 *
 * NB: The only way to terminate a thread is for its `processMsg` callback to
 * return `false`, or for the process itself to be terminated.
 *
 * @param cfg [in] Description of the thread to create
 */
void SkalThreadCreate(const SkalThreadCfg* cfg);


/** Pause the calling thread until all skal threads have finished
 *
 * This function will not return until all the threads have finished. Use this
 * function to write an application that performs a certain tasks and terminates
 * when finished.
 *
 * **WARNING** You must call this function AFTER having called `SkalInit()`.
 * Also, you MUST NOT call `SkalExit()` while a call to `SkalPause()` is in
 * progress; this will result in a segmentation fault.
 *
 * @return `true` if all threads have finished, `false` if `SkalCancel()` has
 *         been called (in which case, some threads are still probably running)
 */
bool SkalPause(void);


/** Cancel a `SkalPause()`
 *
 * Use this function to cause `SkalPause()` to return immediately. When you call
 * `SkalCancel()`, `SkalPause()` will return `false`.
 *
 * This function is typically called from signal handlers.
 *
 * **WARNING** You must call this function AFTER having called `SkalInit()`.
 * Also, you MUST NOT call `SkalExit()` while a call to `SkalPause()` or
 * `SkalCancel()` is in progress; this will result in a segmentation fault.
 */
void SkalCancel(void);


/** Create an alarm object
 *
 * The current time will be assigned to the alarm object as its timestamp.
 *
 * The current thread will be used as the origin, provided the thread is managed
 * by skal. If not, no origin will be set.
 *
 * The alarm comment can be a UTF-8 string.
 *
 * @param name     [in] Alarm name; must not be NULL; alarm names starting with
 *                      "skal" are reserved for skal
 * @param severity [in] Alarm severity
 * @param isOn     [in] Whether the condition related to the alarm started or
 *                      finished
 * @param autoOff  [in] Whether the alarm will be turned off automatically, or
 *                      would require external action (i.e. the operator will
 *                      have to turn it off); ignored if `isOn` is `false`
 * @param format   [in] Free-form comment: printf-style format; may be NULL if
 *                      you don't want to add a comment
 * @param ...      [in] Printf-style arguments
 *
 * @return The alarm object; this function never returns NULL
 */
SkalAlarm* SkalAlarmCreate(const char* name, SkalAlarmSeverityE severity,
        bool isOn, bool autoOff, const char* format, ...)
    __attribute__(( format(printf, 5, 6) ));


/** Add a reference to an alarm
 *
 * This will increment the alarm reference counter by one.
 *
 * @param alarm [in,out] Alarm to reference; must not be NULL
 */
void SkalAlarmRef(SkalAlarm* alarm);


/** Remove a reference from an alarm
 *
 * This will decrement the alarm reference counter by one.
 *
 * If the alarm reference counter reaches zero, the alarm is freed. Therefore,
 * always assumes you are the last one to call `SkalAlarmUnref()` and that the
 * alarm has been freed and is no longer available when this function returns.
 *
 * @param alarm [in,out] Alarm to de-reference; must not be NULL; might be freed
 *                       by this call
 */
void SkalAlarmUnref(SkalAlarm* alarm);


/** Get the alarm name
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return The alarm name; never NULL
 */
const char* SkalAlarmName(const SkalAlarm* alarm);


/** Get the alarm severity
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return Alarm severity
 */
SkalAlarmSeverityE SkalAlarmSeverity(const SkalAlarm* alarm);


/** Get the alarm origin (i.e. the thread that raised the alarm)
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return Name of thread that sent the alarm; will be NULL if alarm is created
 *         from a non-skal thread
 */
const char* SkalAlarmOrigin(const SkalAlarm* alarm);


/** Get whether the alarm is on or off
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return `true` if the alarm is on, `false` if it is off
 */
bool SkalAlarmIsOn(const SkalAlarm* alarm);


/** Check whether the alarm can be automatically turned off
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return `true` if the alarm can be turned off automatically, `false` if it
 *         has to be turned off by the operator
 */
bool SkalAlarmAutoOff(const SkalAlarm* alarm);


/** Get alarm timestamp
 *
 * This is the number of micro-seconds since the Epoch.
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return Alarm timestamp (us since Epoch)
 */
int64_t SkalAlarmTimestamp_us(const SkalAlarm* alarm);


/** Get the alarm comment
 *
 * @param alarm [in] Alarm to query; must not be NULL
 *
 * @return Alarm comment, or NULL if no comment
 */
const char* SkalAlarmComment(const SkalAlarm* alarm);


/** Make a copy of an alarm
 *
 * @param alarm [in] Alarm to copy; must not be NULL
 *
 * @return A copy of `alarm`; this function never returns NULL
 */
SkalAlarm* SkalAlarmCopy(SkalAlarm* alarm);


/** Create a new blob
 *
 * The blob's reference counter will be set to 1.
 *
 * This function will also create a blob proxy, which will also have a reference
 * counter set to 1. When you are finished with the blob proxy, please call
 * `SkalBlobProxyUnref()` on it.
 *
 * @param allocatorName [in] Allocator to use to create the blob. This may be
 *                           NULL, or the empty string, in which case the
 *                           "malloc" allocator will be used.
 * @param id            [in] Blob identifier; NULL may or may not be a valid
 *                           value depending on the allocator.
 * @param size_B        [in] Minimum number of bytes to allocate; <= 0 may or
 *                           may not be allowed, depending on the allocator.
 *
 * The following allocators are always available:
 *  - "malloc" (which is used when the `allocator` argument is NULL): This
 *    allocates memory using `malloc()`. The `id` argument is ignored; `size_B`
 *    must be > 0. If in doubt, use this allocator.
 *  - "shm": This allocates shared memory using `shm_open()`, etc. This type of
 *    memory can be accessed by various processes within the same computer. If
 *    you know you are creating a blob that will be sent to another process,
 *    creating it using the "shm" allocator will gain a bit of time because it
 *    will avoid an extra step of converting a "malloc" blob to a "shm" blob.
 *    The `id` argument is the name of the shared memory area to create (it must
 *    be unique; if the name refers to an existing blob, no new blob will be
 *    created and this function will return NULL); `size_B` must be > 0.
 *
 * The blob will automatically be converted to a blob of a wider scope if
 * necessary. For example, a "malloc" blob will be converted to a "shm" blob if
 * it is sent to a different process. This is completely transparent to you.
 * However, please note that there is no "system" level allocator provided by
 * default. Blobs sent to a different computer will be copied over the network.
 *
 * Here is the typical life cycle of a blob:
 *  - It is created through `SkalBlobCreate()`
 *  - It is mapped through `SkalBlobMap()`
 *  - It is populated with some data
 *  - It is unmapped using `SkalBlobUnmap()`
 *  - It is attached to a message with `SkalMsgAttachBlob()` (at which point the
 *    ownership of the blob is transferred to the message)
 *  - The message is sent by the sender and received by the recipient
 *  - The recipient can get a proxy to the blob by calling `SkalMsgDetachBlob()`
 *  - The blob is mapped in the recipient's memory space with `SkalBlobMap()`
 *  - The blob is read or worked on
 *  - It is unmapped with `SkalBlobUnmap()`
 *  - The blob proxy is de-allocated with a call to `SkalBlobProxyUnref()`;
 *    this will also decrement the blob's reference counter, and de-allocate the
 *    blob if it was the last reference
 *  - If you need multiple access to the blob for some reason, you can do it by
 *    calling `SkalBlobDupProxy()` as many times as needed
 *
 * **VERY IMPORTANT** Skal does not provide any mechanism to allow exclusive
 * access to the memory area pointed to by a blob (no mutex, no semaphore, etc.)
 * as this would go against skal philosophy because message processors are not
 * allowed to block. It is expected that the application designer will be
 * sensible in how the application is designed.
 *
 * Let's expand a bit on the previous remark. Simultaneous access to a blob
 * could happen if the same blob is sent to 2 or more threads simultaneously.
 * There are 2 ways a blob can be sent to more than one thread:
 *  - A blob is referenced by different messages, and the messages are sent to
 *    their respective threads (that also include the case where a message is
 *    referenced many times and sent to different recipients)
 *  - A blob is referenced by only one message, and the message is sent to a
 *    group which has 2 or more subscribers
 *
 * The following use cases will not require any special attention:
 *  - The blob is sent to only one thread
 *  - The blob is sent to 2 or more threads which will not modify it
 *
 * The following use case will require special attention: the blob is sent to
 * more than one thread, one of them will write to the memory area pointed to by
 * the blob. You would then have a race condition between the writing thread and
 * the reading thread(s). This situation would essentially show an application
 * design that would run against skal philosophy. Either:
 *  - the data needs to be modified before it is read; in which case the writing
 *    thread should by the only recipient and forward the blob after
 *    modification
 *  - it does not matter whether the data is modified before or after it is
 *    read; the same as above applies here
 *  - the data needs to be modified after it is read; in this case, a more
 *    complex mechanism for "joining" the paths the blob takes would be
 *    required; this would be a very uncommon scenario, though
 *
 * @return The blob proxy, or NULL in case of error (the allocator does not
 *         exist, invalid `id` or `size_B` for the chosen allocator, or failed
 *         to allocate)
 */
SkalBlobProxy* SkalBlobCreate(const char* allocatorName,
        const char* id, int64_t size_B);


/** Open an existing blob
 *
 * The blob's reference counter will be incremented.
 *
 * This function will also create a blob proxy, which will have a reference
 * counter set to 1. When you are finished with the blob proxy, please call
 * `SkalBlobProxyUnref()` on it.
 *
 * @param allocatorName [in] Allocator to use to open the blob. This may be
 *                           NULL, or the empty string, in which case the
 *                           "malloc" allocator will be used.
 * @param id            [in] Blob identifier for the allocator. NULL is normally
 *                           not allowed, but that would depend on the
 *                           allocator.
 *
 * @return The blob proxy, or NULL in case of error (the allocator does not
 *         exist, the blob does not exist or invalid `id`)
 */
SkalBlobProxy* SkalBlobOpen(const char* allocatorName, const char* id);


/** Add a reference to a blob proxy
 *
 * @param proxy [in,out] Blob proxy to reference
 */
void SkalBlobProxyRef(SkalBlobProxy* proxy);


/** Remove a reference to a blob proxy
 *
 * The blob's reference counter will be decremented, so it might be de-allocated
 * as a result. If the blob proxy is de-allocated, the reference counter of the
 * underlying blob will be decremented (potentially triggering the de-allocation
 * of the blob itself).
 *
 * @param proxy [in,out] Blob proxy to unreference; must not be NULL
 */
void SkalBlobProxyUnref(SkalBlobProxy* proxy);


/** Add a reference to a blob
 *
 * This will increment the blob's reference counter by one. You should normally
 * not call this function unless you know what you are doing.
 *
 * @param proxy [in,out] Proxy to blob to reference; must not be NULL
 */
void SkalBlobRef(SkalBlobProxy* proxy);


/** Remove a reference to a blob
 *
 * This will decrement the blob's reference counter by one. You should normally
 * not call this function unless you know what you are doing.
 *
 * @param proxy [in,out] Proxy to blob to de-reference; must not be NULL
 */
void SkalBlobUnref(SkalBlobProxy* proxy);


/** Map the blob's memory into the current process memory
 *
 * You have to map the blob in order to read/write the memory area it
 * represents.
 *
 * *You do not have exclusive access to the blob*, please refer to
 * `SkalBlobCreate()` for more information.
 *
 * **You MUST call `SkalBlobUnmap()` when finished with it, and before any other
 * `SkalBlob*()` functions. You are not allowed to block between a call to
 * `SkalBlobMap()` and `SkalBlobUnmap()`.**
 *
 * @param proxy [in,out] Proxy to blob to map; must not be NULL
 *
 * @return A pointer to the mapped memory area, or NULL in case of error
 */
uint8_t* SkalBlobMap(SkalBlobProxy* proxy);


/** Unmap the blob's memory from the current process memory
 *
 * Always call this function as soon as possible after a call to
 * `SkalBlobMap()`.
 *
 * @param proxy [in,out] Proxy to blob to unmap; must not be NULL
 */
void SkalBlobUnmap(SkalBlobProxy* proxy);


/** Get the blob's id
 *
 * @param proxy [in] Proxy to blob to query; must not be NULL
 *
 * @return The blob id, which may be NULL
 */
const char* SkalBlobId(const SkalBlobProxy* proxy);


/** Get the blob's size, in bytes
 *
 * @param proxy [in] Proxy to blob to query; must not be NULL
 *
 * @return The blob's size, in bytes, which may be 0 (but never <0)
 */
int64_t SkalBlobSize_B(const SkalBlobProxy* proxy);


/** Duplicate a blob proxy
 *
 * The new blob proxy will point to the same blob. The new blob proxy will have
 * its reference counter set to 1.
 *
 * @param proxy [in] Blob proxy to duplicate; must not be NULL
 *
 * @return A duplicate blob proxy; this function never returns NULL
 */
SkalBlobProxy* SkalBlobDupProxy(const SkalBlobProxy* proxy);


/** Create an empty message
 *
 * The time-to-live (TTL) counter works in the same way as for IP TTL. It will
 * be decremented each time the message goes through a skald daemon. The message
 * will be dropped if its TTL reaches zero.
 *
 * @param name      [in] Message name. This argument may not be NULL and may
 *                       not be an empty string. Please note that message names
 *                       starting with "skal" are reserved for skal's own use,
 *                       so please avoid prefixing your message names with
 *                       "skal".
 * @param recipient [in] Whom to send this message to; must not be NULL; may be
 *                       a thread or a multicast group (for the latter, you must
 *                       also set the `SKAL_MSG_FLAG_MULTICAST` flag)
 * @param flags     [in] Message flags; please refer to `SKAL_MSG_FLAG_*`
 * @param ttl       [in] Time-to-live counter initial value; <=0 for default
 *
 * @return The newly created skal message; this function never returns NULL
 */
SkalMsg* SkalMsgCreateEx(const char* name, const char* recipient,
        uint8_t flags, int8_t ttl);


/** Create a simple message
 *
 * This is a simplified version of `SkalMsgCreateEx()`, which takes out
 * often-unused arguments. The created message will have no flags set and a TTL
 * set to the default value.
 *
 * @param name      [in] Message name; same as `SkalMsgCreateEx()`
 * @param recipient [in] Message recipient; same as `SkalMsgCreateEx()`
 *
 * @return Created skal message; this function never returns NULL
 */
SkalMsg* SkalMsgCreate(const char* name, const char* recipient);


/** Create a simple message where the recipient is a group
 *
 * This is a simplified version of `SkalMsgCreateEx()`, which takes out
 * often-unused arguments. The created message will have the
 * `SKAL_MSG_FLAG_MULTICAST` flag set and a TTL set to the default value.
 *
 * @param name      [in] Message name; same as `SkalMsgCreateEx()`
 * @param recipient [in] Message recipient; same as `SkalMsgCreateEx()`
 *
 * @return Created skal message; this function never returns NULL
 */
SkalMsg* SkalMsgCreateMulticast(const char* name, const char* recipient);


/** Add a reference to a message
 *
 * This will increment the message reference counter by one.
 *
 * @param msg [in,out] Message to reference; must not be NULL
 */
void SkalMsgRef(SkalMsg* msg);


/** Remove a reference from a message
 *
 * This will decrement the message reference counter by one.
 *
 * If the message reference counter reaches zero, the message is freed.
 * Therefore, always assumes you are the last one to call `SkalMsgUnref()` and
 * that the message has been freed and is no longer available when this function
 * returns.
 *
 * @param msg [in,out] Message to de-reference; must not be NULL; might be
 *                     freed by this call
 */
void SkalMsgUnref(SkalMsg* msg);


/** Get the message id
 *
 * This is a unique identifier for messages created on this computer.
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message id
 */
int64_t SkalMsgId(const SkalMsg* msg);


/** Get the time of the message's birth
 *
 * The returned timestamp is the number of micro-seconds since the Epoch.
 *
 * @return Timestamp of when the message has been created
 */
int64_t SkalMsgTimestamp_us(const SkalMsg* msg);


/** Get the message name
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message name; never NULL
 */
const char* SkalMsgName(const SkalMsg* msg);


/** Get the message sender
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message sender; never NULL
 */
const char* SkalMsgSender(const SkalMsg* msg);


/** Get the message recipient
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message recipient; never NULL
 */
const char* SkalMsgRecipient(const SkalMsg* msg);


/** Get the message flags
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message flags
 */
uint8_t SkalMsgFlags(const SkalMsg* msg);


/** Get the message TTL
 *
 * @param msg [in] Message to query; must not be NULL
 *
 * @return The message TTL
 */
int8_t SkalMsgTtl(const SkalMsg* msg);


/** Decrement the message TTL
 *
 * @param msg [in,out] Message whose TTL to decrement by 1
 */
void SkalMsgDecrementTtl(SkalMsg* msg);


/** Add an extra integer to the message
 *
 * @param msg  [in,out] Message to manipulate; must not be NULL
 * @param name [in]     Name of the integer; must not be NULL
 * @param i    [in]     Integer to add
 */
void SkalMsgAddInt(SkalMsg* msg, const char* name, int64_t i);


/** Add an extra floating-point number to the message
 *
 * @param msg  [in,out] Message to manipulate; must not be NULL
 * @param name [in]     Name of the double; must not be NULL
 * @param d    [in]     Double to add
 */
void SkalMsgAddDouble(SkalMsg* msg, const char* name, double d);


/** Add an extra string to the message
 *
 * @param msg  [in,out] Message to manipulate; must not be NULL
 * @param name [in]     Name of the string; must not be NULL
 * @param s    [in]     String to add; must not be NULL; must be UTF-8 encoded
 *                      and null-terminated; can be of arbitrary length.
 */
void SkalMsgAddString(SkalMsg* msg, const char* name, const char* s);


/** Add a formatted string to the message
 *
 * @param msg    [in,out] Message to manipulate; must not be NULL
 * @param name   [in]     Name of the string; must not be NULL
 * @param format [in]     Printf-like format of string to send; must not be
 *                        NULL; must be UTF-8 encoded and null-terminated; can
 *                        be of arbitrary length.
 * @param ...    [in]     Printf-like arguments
 */
void SkalMsgAddFormattedString(SkalMsg* msg, const char* name,
        const char* format, ...)
    __attribute__(( format(printf, 3, 4) ));


/** Add an extra binary field to the message
 *
 * Unlike a blob, this field will be copied every time a message moves from one
 * process to another. So this would be suitable from small data (a few kiB at
 * most).
 *
 * @param msg      [in,out] Message to manipulate; must not be NULL
 * @param name     [in]     Name of the string; must not be NULL
 * @param miniblob [in]     Data to add; must not be NULL
 * @param size_B   [in]     Number of bytes to add; must be > 0
 */
void SkalMsgAddMiniblob(SkalMsg* msg, const char* name,
        const uint8_t* miniblob, int size_B);


/** Attach a blob to a message
 *
 * You will lose ownership of the `blob` proxy. Do not call `SkalBlobClose()` on
 * it, do not touch the blob any more.
 *
 * @param msg  [in,out] Message to modify; must not be NULL
 * @param blob [in,out] Blob to attach; must not be NULL
 */
void SkalMsgAttachBlob(SkalMsg* msg, SkalBlobProxy* blob);


/** Attach an alarm to a message
 *
 * The ownership of the alarm is transferred to the `msg`. If you want to
 * continue accessing the alarm after this call, you need to take a reference
 * first, by calling `SkalAlarmRef(alarm)`.
 *
 * @param msg   [in,out] Message to modify; must not be NULL
 * @param alarm [in,out] Alarm to attached; must not be NULL
 */
void SkalMsgAttachAlarm(SkalMsg* msg, SkalAlarm* alarm);


/** Check if a message has a field with the given name
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasField(const SkalMsg* msg, const char* name);


/** Check if a message has an integer field with the given name
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasInt(const SkalMsg* msg, const char* name);


/** Check if a message has a double field with the given name
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasDouble(const SkalMsg* msg, const char* name);


/** Check if a message has a string field with the given name
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasString(const SkalMsg* msg, const char* name);


/** Check if a message has an ASCII string field with the given name
 *
 * Like `SkalMsgHasStringField()`, but also checks the string is an ASCII
 * string.
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasAsciiString(const SkalMsg* msg, const char* name);


/** Check if a message has a miniblob field with the given name
 *
 * @param msg  [in] Message to check; must not be NULL
 * @param name [in] Name of the field to check; must not be NULL
 */
bool SkalMsgHasMiniblob(const SkalMsg* msg, const char* name);


/** Get the value of an integer previously added to a message
 *
 * @param msg  [in] Message to query; must not be NULL
 * @param name [in] Name of the integer; must exists in this `msg`
 *
 * @return The value of the integer
 */
int64_t SkalMsgGetInt(const SkalMsg* msg, const char* name);


/** Get the value of a double previously added to a message
 *
 * @param msg  [in] Message to query; must not be NULL
 * @param name [in] Name of the double; must exists in this `msg`
 *
 * @return The value of the double
 */
double SkalMsgGetDouble(const SkalMsg* msg, const char* name);


/** Get the value of a string previously added to a message
 *
 * @param msg  [in] Message to query; must not be NULL
 * @param name [in] Name of the string; must exists in this `msg`
 *
 * @return The value of the string; this function never returns NULL
 */
const char* SkalMsgGetString(const SkalMsg* msg, const char* name);


/** Get the value of a binary field previously added to a message
 *
 * If the supplied `buffer` is too small, the binary data is truncated to fit
 * inside `buffer`. In any case, this function returns the size of the binary
 * data, which might be greater than the size of `buffer`, if it was too small.
 *
 * @param msg    [in]  Message to query; must not be NULL
 * @param name   [in]  Name of the binary field; must exists in this `msg`
 * @param size_B [out] Number of bytes in the miniblob
 *
 * @return Read-only pointer to the miniblob; do NOT call `free()` on it!
 */
const uint8_t* SkalMsgGetMiniblob(const SkalMsg* msg, const char* name,
        int* size_B);


/** Detach a blob from a message
 *
 * This function creates a blob proxy, in the same way as `SkalBlobOpen()` (and
 * the blob's reference counter will be incremented). You must call
 * `SkalBlobClose()` on the blob proxy when finished.
 *
 * You can call this function multiple times to extract all the blobs from the
 * message. The order in which the blobs are detached is random.
 *
 * @param msg  [in] Message to query; must not be NULL
 * @param name [in] Name of the blob in this msg; must exists in this `msg`;
 *                  please note this is not the blob id, but the field name as
 *                  set in `SkalMsgAddBlob()`
 *
 * @return The blob proxy; this function never returns NULL
 */
SkalBlobProxy* SkalMsgDetachBlob(const SkalMsg* msg);


/** Detach an alarm from a message
 *
 * You can call this function multiple times to extract all the alarms from the
 * message. The ownership of the alarm is transferred to you, please call
 * `SkalAlarmUnref()` when you're finished with it.
 *
 * @param msg [in,out] Message to manipulate; must not be NULL
 *
 * @return The next alarm, or NULL if no more alarm
 */
SkalAlarm* SkalMsgDetachAlarm(SkalMsg* msg);


/** Make a copy of a message
 *
 * Please note the copied message will have the same TTL value as `msg`; in
 * other words, the TTL value of the copied message is not reset to the original
 * TTL value.
 *
 * @param msg        [in] Message to copy; must not be NULL
 * @param copyBlobs  [in] If set to `false`, the new message will not have any
 *                        blob. If set to `true`, all the blobs in `msg` will be
 *                        referenced and attached to the message copy.
 * @param copyAlarms [in] Whether to also copy the alarms
 * @param recipient  [in] Recipient for this new message; may be NULL to keep
 *                        the same recipient as `msg`
 *
 * @return The copied message; this function never returns NULL
 */
SkalMsg* SkalMsgCopyEx(const SkalMsg* msg,
        bool copyBlobs, bool copyAlarms, const char* recipient);


/** Make a simple copy of a message
 *
 * This function is like `SkalMsgCopyEx()` with both `copyBlobs` and
 * `copyAlarms` set to `true`.
 */
SkalMsg* SkalMsgCopy(const SkalMsg* msg, const char* recipient);


/** Send a message to its recipient
 *
 * You will lose ownership of the message. Please note the message is *always*
 * successfully sent. It might get lost in transit, though.
 *
 * Please note there is a slight chance this function might block. If the
 * recipient is outside this process, the message is sent to skald through a
 * UNIX socket. If skald is overloaded, sending the message might block until
 * skald can receive messages again; skald is architectured to be very fast, so
 * you should not be blocked for long.
 *
 * @param msg [in,out] Message to send
 */
void SkalMsgSend(SkalMsg* msg);


/** Get the current domain name
 *
 * The domain name is sent by skald just after we connect to it. As soon as
 * `SkalInit()` returns, the domain name is set.
 *
 * @return The current domain name, or NULL if not known yet
 */
const char* SkalDomain(void);



/* @} */

#ifdef __cplusplus
}
#endif

#endif /* SKAL_h_ */
