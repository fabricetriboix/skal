/* Copyright Fabrice Triboix - Please read the LICENSE file */

#pragma once

#include "skal-cfg.hpp"
#include <cstdint>
#include <string>
#include <memory>
#include <stdexcept>
#include <boost/noncopyable.hpp>

namespace skal {

class blob_allocator_t;

/** Class to provide access to a blob
 *
 * This is a base class which must be derived from to implement the pure
 * virtual methods according to the underlying blob allocator. Your derived
 * class should increment the blob's reference counter when a proxy is
 * constructed, and unreference it when it's destructed.
 *
 * When a proxy to a blob is created, that will increment the blob's reference
 * counter. When the proxy is destroyed, the blob's reference counter will be
 * decremented (and the blob potentially destroyed if that was the last
 * reference).
 */
class blob_proxy_t : boost::noncopyable
{
public :
    /** Destructor
     *
     * The destructor must decrement the blob's reference counter.
     */
    virtual ~blob_proxy_t() = default;

    /** Get the blob id */
    virtual const std::string& id() const = 0;

    /** Get the blob size */
    virtual int64_t size_B() const = 0;

    /** Manually increment the reference counter of the underlying blob */
    void ref();

    /** Manually decrement the reference counter of the underlying blob
     *
     * The underlying blob will be destroyed if this counter reaches zero.
     */
    void unref();

    /** Structure to map the blob in a RAII fashion
     *
     * The lifetime of this structure must be as short as possible, in order
     * to allow other workers to access the blob too.
     */
    struct scoped_map_t {
        scoped_map_t(blob_proxy_t& proxy) : proxy_(proxy), mem_(proxy_.map())
        {
            proxy_.is_mapped_ = true;
        }

        ~scoped_map_t()
        {
            proxy_.unmap();
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

protected :
    /** Constructor for this base class
     *
     * The constructor must increment the blob's reference counter.
     *
     * Accessible only from derived classes.
     */
    blob_proxy_t() : is_mapped_(false) { }

private :
    /** Increment the reference counter of the underlying blob
     *
     * You are guaranteed that the blob is mapped when this method is called.
     */
    virtual void do_ref() = 0;

    /** Decrement the reference counter of the underlying blob
     *
     * The underlying blob will be destroyed if this counter reaches zero.
     *
     * You are guaranteed that the blob is mapped when this method is called.
     */
    virtual void do_unref() = 0;

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
     */
    virtual void* map() = 0;

    /** Unmap a blob from the caller's address space
     *
     * This method is the pendent of `map()` and must be called as quickly as
     * possible after `map()` has been called.
     */
    virtual void unmap() = 0;

    bool is_mapped_; /**< Is the blob currently mapped? */
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
     * \param name  [in] Allocator name; must be unique within the allocator's
     *                   scope
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
     * This function must throw if the blob already exists.
     *
     * \param id     [in] Blob identifier (eg: buffer slot on a video card,
     *                    shared memory id, etc.)
     * \param size_B [in] Minimum size of the area to allocate
     *
     * \return A proxy to the created blob, or an empty pointer in case of
     *         error (typically because a blob with the same `id` already
     *         exists)
     */
    virtual std::unique_ptr<blob_proxy_t> create(const std::string& id,
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
     * This method must throw if the blob does not already exist.
     *
     * \param id [in] Blob identifier
     *
     * \return A proxy to the opened blob, or an empty pointer in case of
     *         error (typically because there is no blob `id`)
     */
    virtual std::unique_ptr<blob_proxy_t> open(const std::string& id) = 0;

private :
    std::string name_;
    scope_t scope_;
};

/** Add a custom allocator
 *
 * Allocator are uniquely identified by their names. If you add a custom
 * allocator which has the same name as an existing allocator, your allocator
 * will replace the old one.
 *
 * \param allocator [in] Allocator to add
 */
void add_allocator(std::unique_ptr<blob_allocator_t> allocator);

/** Find a custom allocator
 *
 * \param allocator_name [in] Name of allocator to find
 *
 * \return A reference to the found allocator
 */
blob_allocator_t& find_allocator(const std::string& allocator_name);

} // namespace skal
