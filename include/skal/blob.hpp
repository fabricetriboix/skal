/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include <skal/cfg.hpp>
#include <skal/error.hpp>
#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>
#include <boost/noncopyable.hpp>

namespace skal {

/** Exception class: try to open an invalid blob, or blob is corrupted */
struct bad_blob : public error
{
    bad_blob() : error("skal::bad_blob") { }
};

class blob_allocator_t;
class blob_proxy_t;

/** Helper function to create a blob
 *
 * The `allocator_name` may be one of "malloc", "shm", or any custom allocator
 * that had been registered using the `register_allocator()` function.
 *
 * Depending on the chosen allocator, the `id` and `size_B` arguments may or
 * may not be used by the allocator.
 *
 * \param allocator_name [in] Name of allocator to use
 * \param id             [in] Blob id
 * \param size_B         [in] Blob size
 *
 * \return A proxy to the created blob, never an empty pointer
 *
 * \throw `std::out_of_range` if there is no allocator with that name
 *
 * \throw `bad_blob` if a blob with the same `id` already exists, or if the
 *        blob can't be created for some reason
 */
blob_proxy_t create_blob(const std::string& allocator_name,
        const std::string& id, int64_t size_B);

/** Helper function to open a blob
 *
 * \param allocator_name [in] Name of allocator to use
 * \param id             [in] Blob id
 *
 * \return A proxy to the opened blob, never an empty pointer
 *
 * \throw `std::out_of_range` if there is no allocator with that name
 *
 * \throw `bad_blob` if blob `id` does not exist or can't be opened for some
 *        reason
 */
blob_proxy_t open_blob(const std::string& allocator_name,
        const std::string& id);

/** Base class which represents a proxy to a blob of a certain type
 *
 * This is a base class which must be derived from to implement the pure
 * virtual methods according to the underlying blob allocator. Your derived
 * class should increment the blob's reference counter when a proxy is
 * constructed, and unreference it when it's destructed.
 */
class proxy_base_t : boost::noncopyable
{
public :
    /** Destructor
     *
     * The destructor must decrement the blob's reference counter, and thus
     * potentially destroy the underlying blob.
     */
    virtual ~proxy_base_t() = default;

    /** Get the allocator that created this proxy
     *
     * \return The blob allocator that created this proxy
     */
    blob_allocator_t& allocator() const
    {
        return allocator_;
    }

    /** Get the blob id */
    virtual const std::string& id() const = 0;

    /** Get the blob size */
    virtual int64_t size_B() const = 0;

    /** Increment the reference counter of the underlying blob
     *
     * You are guaranteed that the blob is mapped when this method is called.
     */
    virtual void ref() = 0;

    /** Decrement the reference counter of the underlying blob
     *
     * The underlying blob will be destroyed if this counter reaches zero.
     *
     * You are guaranteed that the blob is mapped when this method is called.
     */
    virtual void unref() = 0;

    /** Map a blob into the caller's address space
     *
     * This method will be called to allow the current process/thread to read
     * and/or write the blob's memory area.
     *
     * It is strongly advised that a mutual exclusion mechanism is implemented.
     * This way, if a blob is already mapped, a call to `map()` would block
     * until the blob is unmapped. Alternatively, trying to map a blob that is
     * already mapped could throw an exception. In any case, there must be at
     * most one mapping active at any one time.
     *
     * \return Pointer to the mapped memory area
     *
     * \throw `bad_blob` if the blob has been corrupted
     */
    virtual void* map() = 0;

    /** Unmap a blob from the caller's address space
     *
     * This method is the pendent of `map()` and must be called as quickly as
     * possible after `map()` has been called.
     *
     * \throw `bad_blob` if the blob has been corrupted
     */
    virtual void unmap() = 0;

protected :
    /** Constructor for this base class
     *
     * The constructor must increment the blob's reference counter.
     *
     * Accessible only from derived classes.
     *
     * \param allocator [in] Reference to the allocator that created or
     *                       opened this blob proxy
     */
    proxy_base_t(blob_allocator_t& allocator) : allocator_(allocator)
    {
    }

private :
    blob_allocator_t& allocator_; /**< Allocator that created/open this proxy */
};

/** Class providing access to a blob
 *
 * This class is copyable and assignable.
 */
class blob_proxy_t final
{
public :
    blob_proxy_t() = default;
    ~blob_proxy_t() = default;

    blob_proxy_t(std::unique_ptr<proxy_base_t> base_proxy)
        : base_proxy_(std::move(base_proxy)), is_mapped_(false)
    {
        skal_assert(base_proxy_);
    }

    /** Copy constructor
     *
     * You are not allowed to copy a mapped proxy.
     */
    blob_proxy_t(const blob_proxy_t& right);

    /** Move constructor */
    blob_proxy_t(blob_proxy_t&& right) : is_mapped_(false)
    {
        base_proxy_ = std::move(right.base_proxy_);
    }

    friend void swap(blob_proxy_t& left, blob_proxy_t& right)
    {
        using std::swap;
        swap(left.base_proxy_, right.base_proxy_);
    }

    /** Copy-assignment operator
     *
     * You are not allowed to copy a mapped proxy.
     */
    blob_proxy_t& operator=(const blob_proxy_t& right)
    {
        blob_proxy_t tmp(right);
        base_proxy_ = std::move(tmp.base_proxy_);
        is_mapped_ = false;
        return *this;
    }

    /** Move-assignment operator */
    blob_proxy_t& operator=(blob_proxy_t&& right)
    {
        base_proxy_ = std::move(right.base_proxy_);
        is_mapped_ = false;
        return *this;
    }

    blob_allocator_t& allocator() const
    {
        skal_assert(base_proxy_);
        return base_proxy_->allocator();
    }

    const std::string& id() const
    {
        skal_assert(base_proxy_);
        return base_proxy_->id();
    }

    int64_t size_B() const
    {
        skal_assert(base_proxy_);
        return base_proxy_->size_B();
    }

    /** Increment the reference counter of the underlying blob
     *
     * The blob may or may not be mapped when you call this method.
     */
    void ref();

    /** Decrement the reference counter of the underlying blob
     *
     * The blob may or may not be mapped when you call this method.
     *
     * NB: Logically (and unless there is a bug in your code), it is impossible
     * for the reference counter to reach zero in this method, because the
     * proxy itself holds a reference to the blob until it is destroyed.
     */
    void unref();

    /** Structure to map the blob in a RAII fashion
     *
     * The lifetime of this structure must be as short as possible in order
     * to allow other workers to access the blob too. An object of type
     * `scoped_map_t` must have a shorter life span than the `proxy` it is
     * working on (you can expect a nasty crash otherwise).
     */
    struct scoped_map_t {
        /** Constructor
         *
         * \param proxy [in,out] Proxy to the blob to map
         *
         * \throw `bad_blob` if the underlying blob has been corrupted or
         *        can't be mapped for some reason
         */
        scoped_map_t(blob_proxy_t& proxy)
            : proxy_(proxy), mem_(proxy_.base_proxy_->map())
        {
            proxy_.is_mapped_ = true;
        }

        ~scoped_map_t()
        {
            proxy_.base_proxy_->unmap();
            proxy_.is_mapped_ = false;
        }

        void* mem() const
        {
            return mem_;
        }

    private :
        blob_proxy_t& proxy_;
        void* mem_;
    };

private :
    std::unique_ptr<proxy_base_t> base_proxy_;
    bool is_mapped_;
};

/** Class representing a blob allocator
 *
 * A custom blob allocator could be used, for example, to allocate frame
 * buffers on a video card, network packets from a network processor, and other
 * such exotic memory areas that do not belong to the computer's RAM.
 *
 * Please note that skal already provides the following allocators by default:
 *  - "malloc": Allocates RAM accessible from within the process (it uses
 *    `malloc()` to do so)
 *  - "shm": Allocates RAM accessible from different processes (it uses the
 *    operating system shared memory capabilities)
 *
 * If you want to create your own custom blob allocator, you must derive from
 * this base class and override the pure virtual methods.
 */
class blob_allocator_t : boost::noncopyable
{
public :
    enum class scope_t {
        /** Scope is limited to the current process; eg: "malloc" allocator */
        process,

        /** Scope is the current machine; eg: "shm" allocator */
        computer,

        /** Scope is the current system; eg: NAS-backed object */
        system
    };

    /** Allocator base class constructor
     *
     * \param name  [in] Allocator name; must be unique within the
     *                   allocator's scope
     * \param scope [in] Allocator scope
     */
    blob_allocator_t(const std::string& name, scope_t scope)
        : name_(name), scope_(scope)
    {
    }

    virtual ~blob_allocator_t() = default;

    const std::string& name() const
    {
        return name_;
    }

    scope_t scope() const
    {
        return scope_;
    }

    /** Create a blob
     *
     * This method must create a new blob, and also creates a blob proxy to
     * access the blob.
     *
     * Whether the arguments are used or not depends on the allocator. For the
     * "malloc" allocator, `id` is ignored and `size_B` must be >0. For the
     * "shm" allocator, `id` is a unique name for the blob, and `size_B` must
     * be >0.
     *
     * The created blob must have an internal reference counter, which should
     * be initialised to 0 by this method. For the avoidance of doubt, the
     * reference counter is actually 1 when this function returns, as the
     * created proxy holds a reference to the blob. If the returned proxy is
     * immediately destroyed, the blob will be destroyed as well. If you want
     * the blob to survive the destruction of the proxy, you will have to call
     * `blob_proxy_t::ref()` to increment the blob's reference counter.
     *
     * \param id     [in] Blob identifier (eg: buffer slot on a video card,
     *                    shared memory id, etc.)
     * \param size_B [in] Minimum size of the area to allocate
     *
     * \return A proxy to the created blob, never an empty pointer
     *
     * \throw `bad_blob` if the blob already exists or can't be created for
     *        some reason
     */
    virtual std::unique_ptr<proxy_base_t> create(const std::string& id,
            int64_t size_B) = 0;

    /** Open an existing blob
     *
     * This method opens an existing blob, and creates a blob proxy to access
     * the blob.
     *
     * Whether the arguments are used or not depends on the allocator. For both
     * the "malloc" and the "shm" allocator, the `id` represent a unique
     * identifier for the blob.
     *
     * \param id [in] Blob identifier
     *
     * \return A proxy to the opened blob, never an empty pointer
     *
     * \throw `bad_blob` if blob does not exist or has been corrupted
     */
    virtual std::unique_ptr<proxy_base_t> open(const std::string& id) = 0;

private :
    std::string name_;
    scope_t scope_;
};

/** Convert a scope enum to a human-readable string
 *
 * \param scope [in] Enum value to convert
 *
 * \return A human-readable string
 */
std::string to_string(blob_allocator_t::scope_t scope);

/** Add a custom allocator
 *
 * Allocator are uniquely identified by their names. The name of your allocator
 * must be unique in this process. Additionally, if the scope of your allocator
 * is greater than `scope_t::process`, it is expected that the same allocator
 * names in the various processes and machines refer to the same underlying
 * blob allocation mechanisms.
 *
 * \param allocator [in] Allocator to add
 */
void register_allocator(std::unique_ptr<blob_allocator_t> allocator);

/** Find an allocator
 *
 * \param allocator_name [in] Name of allocator to find
 *
 * \return A reference to the found allocator
 *
 * \throw `std::out_of_range` if there is no allocator with that name
 */
blob_allocator_t& find_allocator(const std::string& allocator_name);

} // namespace skal
