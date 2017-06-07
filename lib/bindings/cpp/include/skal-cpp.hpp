/* Copyright (c) 2017  Fabrice Triboix
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

#pragma once

#include "skal.h"
#include <cstdint>
#include <vector>
#include <string>
#include <memory>
#include <functional>


namespace skal {

/** This SKAL binding for C++ is guaranteed not to throw anywhere */

class Msg; // Forward declaration


/** Base class for a custom blob allocator
 *
 * SKAL provides 2 default allocators:
 *  - "malloc": allocate memory using `malloc(3)`
 *  - "shm": allocate shared memory using `shm_open(3)`, etc.
 *
 * If you still want your own blob allocator (because it maps to some hardware
 * buffers, say), you must derive from this class, implement the pure virtual
 * methods, and pass an instance of your allocator to `Init()`.
 */
class Allocator {
public :
    enum class Scope {
        PROCESS,  /**< Suitable for current process; eg: "malloc" allocator */
        COMPUTER, /**< Suitable for current computer; eg: "shm" allocator */
        SYSTEM    /**< Suitable for current system; eg: NAS-backed object */
    };

    /** Constructor
     *
     * @param name  [in] Allocator name; must be unique within the allocator's
     *                   scope
     * @param scope [in] Allocator scope
     */
    Allocator(const std::string& name, Scope scope);

    virtual ~Allocator();

private :
    /** Create a new blob (and a proxy to it)
     *
     * The newly created blob must have its reference counter set to 1.
     *
     * @param id     [in] Optional identifier (eg: buffer slot on a video card)
     * @param size_B [in] Optional minimum size to allocate, in bytes
     *
     * @return A proxy to the newly created blob, or `nullptr` if error
     */
    virtual SkalBlobProxy* create(const std::string& id, int64_t size_B) = 0;

    /** Open an existing blob (and create a proxy to it)
     *
     * The blob's reference counter must be incremented.
     *
     * @param id [in] Optional identifier
     *
     * @return A proxy to the opened blob, or `nullptr` if error
     */
    virtual SkalBlobProxy* open(const std::string& id) = 0;

    /** Close a blob proxy
     *
     * You must free up any resources used by the blob proxy. The blob's
     * reference counter must be decremented. The blob must be de-allocated if
     * its reference counter reaches 0.
     *
     * @param blob [in,out] Proxy to close
     */
    virtual void close(SkalBlobProxy& blob) = 0;

    /** Add a reference to a blob
     *
     * @param blob [in,out] Blob to reference
     */
    virtual void ref(SkalBlobProxy& blob) = 0;

    /** Remove a reference to a blob
     *
     * @param blob [in,out] Blob to unreference
     */
    virtual void unref(SkalBlobProxy& blob) = 0;

    /** Map a blob into the address space of the caller
     *
     * @param blob [in,out] Blob to map
     *
     * @return A pointer to the start of the blob
     */
    virtual uint8_t* map(SkalBlobProxy& blob) = 0;

    /** Unmap a blob
     *
     * @param blob [in,out] Blob to unmap
     */
    virtual void unmap(SkalBlobProxy& blob) = 0;

    /** Get a blob id
     *
     * @param blob [in] Blob to query
     *
     * @return Blob id
     */
    virtual const char* blobid(const SkalBlobProxy& blob) = 0;

    /** Get a blob size, in bytes
     *
     * @param blob [in] Blob to query
     *
     * @return Blob size, in bytes
     */
    virtual int64_t blobsize(const SkalBlobProxy& blob) = 0;

    Allocator(const Allocator&) = delete;
    Allocator& operator=(const Allocator&) = delete;
    Allocator& operator=(Allocator&&) = delete;

    SkalAllocator mAllocator;

    friend bool Init(const char*, std::vector<std::shared_ptr<Allocator>>);
    friend SkalBlobProxy* skalCppAllocatorCreate(void*, const char*, int64_t);
    friend SkalBlobProxy* skalCppAllocatorOpen(void*, const char*);
    friend void skalCppAllocatorClose(void*, SkalBlobProxy*);
    friend void skalCppAllocatorRef(void*, SkalBlobProxy*);
    friend void skalCppAllocatorUnref(void*, SkalBlobProxy*);
    friend uint8_t* skalCppAllocatorMap(void*, SkalBlobProxy*);
    friend void skalCppAllocatorUnmap(void*, SkalBlobProxy*);
    friend const char* skalCppAllocatorBlobId(void*, const SkalBlobProxy*);
    friend int64_t skalCppAllocatorBlobSize(void*, const SkalBlobProxy*);
};


/** Class representing an alarm */
class Alarm final {
public :
    /** Alarm severity */
    enum class SeverityE {
        NOTICE  = SKAL_ALARM_NOTICE,  /**< Important information */
        WARNING = SKAL_ALARM_WARNING, /**< Very important, near miss */
        ERROR   = SKAL_ALARM_ERROR    /**< Something's broken somewhere */
    };

    /** Constructor
     *
     * @param name     [in] Alarm name; names starting with "skal-" are reserved
     * @param severity [in] Alarm severity
     * @param isOn     [in] Turn the alarm on or off
     * @param autoOff  [in] This alarm will be cleared without human interaction
     * @param comment  [in] Free-form comment
     */
    Alarm(const std::string& name, SeverityE severity, bool isOn, bool autoOff,
            const std::string& comment);

    Alarm(const std::string& name, SeverityE severity, bool isOn, bool autoOff)
        : Alarm(name, severity, isOn, autoOff, "")
    {
    }

    Alarm(const std::string& name, SeverityE severity) :
        Alarm(name, severity, true, false, "")
    {
    }

    ~Alarm();

    std::string Name() const;
    SeverityE Severity() const;
    bool IsOn() const;
    bool AutoOff() const;
    std::string Comment() const;

    /** Get the name of the thread that raised this alarm.
     *
     * This will return an empty string if the alarm has been created from a
     * non-skal thread.
     */
    std::string Origin() const;

    /** Get the moment in time when this alarm object has been created */
    int64_t Timestamp_us() const;

private :
    Alarm() = delete;
    Alarm(const Alarm&) = delete;
    Alarm& operator=(const Alarm&) = delete;
    Alarm& operator=(Alarm&&) = delete;

    Alarm(SkalAlarm* alarm);

    SkalAlarm* mAlarm;

    friend class Msg;
};


/** This class represents a proxy to a blob */
class BlobProxy final {
public :
    ~BlobProxy();

    std::string Id() const;
    std::string Name() const;
    int64_t Size_B() const;

    /** This class is used to map the blob into the caller's address space
     *
     * You just need to create an object of type `ScopedMap` and call its
     * `Get()` method. The pointer returned by the `Get()` method points to the
     * first byte of the memory-mapped blob.
     */
    class ScopedMap final {
    public :
        ScopedMap(BlobProxy& blob);
        ~ScopedMap();
        uint8_t* Get() const;
    private :
        BlobProxy* mBlob;
        uint8_t* mPtr;
    };

private :
    BlobProxy() = delete;
    BlobProxy(const BlobProxy&) = delete;
    BlobProxy* operator=(const BlobProxy&) = delete;
    BlobProxy* operator=(BlobProxy&&) = delete;

    /** Private constructor */
    BlobProxy(SkalBlobProxy* BlobProxy);

    SkalBlobProxy* mBlob;

    friend std::shared_ptr<BlobProxy> CreateBlob(const std::string& allocator,
            const std::string& id, int64_t size_B);
    friend std::shared_ptr<BlobProxy> OpenBlob(const std::string& allocator,
            const std::string& id);
    friend class Msg;
};

/** Create a new blob
 *
 * The blob will have its reference counter set to 1.
 *
 * @param allocatorName [in] Name of the allocator to use; may be the empty
 *                           string, in which case the "malloc" allocator is
 *                           used
 * @param id            [in] Identifier for the allocator; may or may not be an
 *                           empty string depending on the chosen allocator
 * @param size_B        [in] Minimum number of bytes to allocate; may or may not
 *                           be <=0 depending on the chosen allocator
 *
 * @return A pointer to a proxy to the blob, or `nullptr` in case of error
 */
std::shared_ptr<BlobProxy> CreateBlob(const std::string& allocator,
        const std::string& id, int64_t size_B);

/** Open an existing blob
 *
 * The blob's reference counter will be incremented.
 *
 * @param allocatorName [in] Name of the allocator to use; may be the empty
 *                           string, in which case the "malloc" allocator is
 *                           used
 * @param id            [in] Identifier for the allocator; may or may not be an
 *                           empty string depending on the chosen allocator
 *
 * @return A pointer to a proxy to the blob, or `nullptr` in case of error
 */
std::shared_ptr<BlobProxy> OpenBlob(const std::string& allocator,
        const std::string& id);



/** Message flag: it's OK to receive this message out of order */
const uint8_t MSG_FLAG_OUT_OF_ORDER_OK = 0x01;

/** Message flag: it's OK to silently drop this message */
const uint8_t MSG_FLAG_DROP_OK = 0x02;

/** Message flag: send this message over a UDP-like link */
const uint8_t MSG_FLAG_UDP = (MSG_FLAG_OUT_OF_ORDER_OK | MSG_FLAG_DROP_OK);

/** Message flag: notify the sender if this packet is dropped */
const uint8_t MSG_FLAG_NTF_DROP = 0x04;

/** This class represents a message */
class Msg final {
public :
    /** Constructor
     *
     * @param name      [in] Message name; names starting with "skal-" are
     *                       reserved for the SKAL framework
     * @param recipient [in] To whom this message should be delivered?
     * @param flags     [in] Flags for this message
     * @param ttl       [in] TTL for this message; <=0 for default
     */
    Msg(const std::string& name, const std::string& recipient,
            uint8_t flags, int8_t ttl);

    Msg(const std::string& name, const std::string& recipient) :
        Msg(name, recipient, 0, 0)
    {
    }

    /** Copy-constructor TODO */
    Msg(const Msg& rhs) = delete;

    /** Copy-assignment operator = TODO */
    Msg& operator=(const Msg& rhs) = delete;

    ~Msg();

    /** Decrement the message TTL */
    void DecrementTtl();

    /** Add an extra integer to the message */
    void AddField(const std::string& name, int64_t i);

    /** Add an extra floating-point number to the message */
    void AddField(const std::string& name, double d);

    /** Add an extra string to the message */
    void AddField(const std::string& name, const std::string& str);

    /** Add an extra binary field to the message
     *
     * This is good for small amounts of data (up to a few KiB). For medium or
     * large amounts of data, a blob will be more efficient because a miniblob
     * is copied every time the message hops somewhere.
     */
    void AddField(const std::string& name, const uint8_t* miniblob, int size_B);

    /** Add an extra blob field to the message
     *
     * @param name [in] Name for this field; please note this is unrelated to
     *                  the blob's id
     * @param blob [in] Proxy to blob to add; must not be `nullptr`
     */
    void AddField(const std::string& name, std::shared_ptr<BlobProxy> blob);

    /** Check if the message has a field with the given name */
    bool HasField(const std::string& name);

    /** Get the value of an integer field
     *
     * This method asserts if a field with that `name` does not exist or is not
     * an integer.
     */
    int64_t GetIntField(const std::string& name) const;

    /** Get the value of a double field
     *
     * This method asserts if a field with that `name` does not exist or is not
     * a double.
     */
    double GetDoubleField(const std::string& name) const;

    /** Get the value of a string field
     *
     * This method asserts if a field with that `name` does not exist or is not
     * a string.
     */
    std::string GetStringField(const std::string& name) const;

    /** Get the value of a miniblob field
     *
     * This method asserts if a field with that `name` does not exist or is not
     * a miniblob. The returned pointer points to a read-only area!
     */
    const uint8_t* GetMiniblobField(const std::string& name, int& size_B) const;

    /** Get access to a blob attached to this message */
    std::shared_ptr<BlobProxy> GetBlob(const std::string& name) const;

    /** Attach an alarm to the message
     *
     * You can call this method repeatidly to attach more than one alarm.
     */
    void AttachAlarm(std::shared_ptr<Alarm> alarm);

    /** Detach an alarm from this message
     *
     * You can call this method repeatidly to detach alarms one by one.
     *
     * @return A shared pointer to the alarm, or `nullptr` if no more alarms
     */
    std::shared_ptr<Alarm> DetachAlarm() const;

    /** Get this message's name */
    std::string Name() const;

    /** Get the name of the thread that sent this message */
    std::string Sender() const;

    /** Get the recipient of this message */
    std::string Recipient() const;

    /** Get the message flags */
    uint8_t Flags() const;

    /** Get the current value of this message's TTL */
    int8_t Ttl() const;

private :
    Msg(SkalMsg* msg);
    SkalMsg* mMsg;

    friend bool skalCppProcessMsg(void*, SkalMsg*);
    friend void Send(Msg&);
};


/** Send a message to its recipient */
void Send(Msg& msg);


/** Create a thread
 *
 * The only way to terminate a thread is for `processMsg` to return `false`.
 *
 * @param name       [in] Thread name; must be unique in the process
 * @param processMsg [in] Function to process a message; return `false` to
 *                        terminate the thread, `true` to carry on
 *
 * The other parameters are quite obscure and it would be unusual to set them to
 * anything but their default values of 0.
 */
void CreateThread(const std::string& name, std::function<bool(Msg&)> processMsg,
        int64_t queueThreshold, int32_t stackSize_B, int64_t xoffTimeout_us);

void CreateThread(const std::string& name,
        std::function<bool(Msg&)> processMsg);


/** Initialise SKAL for this process
 *
 * You MUST call this function before anything else, including creating objects
 * of the various classes in this module.
 *
 * @param skaldUrl   [in] URL to connect to the local skald; this can and should
 *                        be `nullptr`
 * @param allocators [in] Custom allocators if needed
 */
bool Init(const char* skaldUrl,
        std::vector<std::shared_ptr<Allocator>> allocators);

bool Init(const char* skaldUrl);
bool Init();


/** Terminate SKAL for this process
 *
 * This function terminates all threads managed by SKAL in this process and
 * de-allocates all resources used by SKAL.
 *
 * This function is blocking.
 */
void Exit();


/** Get the domain this process runs on
 *
 * @return The SKAL domain
 */
std::string Domain();


/** Pause the calling thread until all SKAL threads have finished
 *
 * @return `true` if all threads have finished, `false` if `Cancel()` has
 *         been called
 */
bool Pause();


/** Cancel a `Pause()` */
void Cancel();


} // namespace skal
