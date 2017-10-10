/* Copyright Fabrice Triboix - Please read the LICENSE file */

#include "skal-blob.hpp"
#include "skal-error.hpp"
#include "detail/skal-log.hpp"
#include "detail/safe-mutex.hpp"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <atomic>
#include <mutex>
#include <map>
#include <sstream>
#include <boost/scoped_ptr.hpp>
#include <boost/interprocess/shared_memory_object.hpp>
#include <boost/interprocess/mapped_region.hpp>
#include <boost/interprocess/sync/interprocess_mutex.hpp>

using boost::interprocess::shared_memory_object;
using boost::interprocess::mapped_region;
using boost::interprocess::interprocess_mutex;
using boost::interprocess::read_write;
using boost::interprocess::create_only;
using boost::interprocess::open_only;

namespace skal {

namespace {


// "malloc" blob allocator
// -----------------------

constexpr const char* malloc_blob_magic = "mallocX";

/** Header at the beginning of a "malloc" blob */
struct malloc_blob_t final
{
    char                 magic[8]; /**< Magic number */
    std::atomic<int64_t> ref;      /**< Reference counter */
    int64_t              size_B;   /**< Size initially requested */

    /** Mutex for exclusive access to the blob */
    ft::safe_mutex<std::mutex> mutex;

    /** Constructor
     *
     * NB: We always assume that whoever constructed this blob holds a
     * reference to it, hence we set the `ref` counter to 1.
     */
    malloc_blob_t(int64_t bytes) : ref(1), size_B(bytes), mutex()
    {
        std::memcpy(magic, malloc_blob_magic, sizeof(magic));
    }

    ~malloc_blob_t()
    {
        // The thread destroying a safe_mutex must not have it locked, so
        // ensure it is unlocked before destroying it.
        mutex.unlock();
    }

    /** Check the "malloc" blob
     *
     * \param id [in] Blob id
     *
     * \throw `bad_blob` if blob header is invalid
     */
    void check(const std::string& id)
    {
        if (this == nullptr) {
            SKAL_LOG(error) << "Invalid 'malloc' blob '" << id
                << "': `this` is a nullptr";
            throw bad_blob();
        }
        if (std::memcmp(magic, malloc_blob_magic, sizeof(magic)) != 0) {
            SKAL_LOG(error) << "Invalid 'malloc' blob '" << id
                << "': wrong magic number";
            throw bad_blob();
        }
        if (size_B <= 0) {
            SKAL_LOG(error) << "Invalid 'malloc' blob '" << id
                << "': invalid size (" << size_B << "): must be >0";
            throw bad_blob();
        }
    }
};

/** Proxy to access a "malloc" blob */
class malloc_proxy_t final : public proxy_base_t
{
public :
    malloc_proxy_t(blob_allocator_t& allocator, malloc_blob_t* blob)
        : proxy_base_t(allocator), blob_(blob)
    {
        SKAL_ASSERT(blob_ != nullptr);
        char buffer[64];
        snprintf(buffer, sizeof(buffer), "%p", blob_);
        id_ = buffer;
        // NB: The blob's reference counter has been incremented for this
        // proxy by the blob allocator, so we don't do it here.
    }

    ~malloc_proxy_t()
    {
        blob_->check(id_);
        if (--blob_->ref <= 0) {
            // NB: Because the malloc blob object has been constructed with a
            // placement-new operation, we have to call the destructor
            // manually.
            blob_->~malloc_blob_t();
            std::free(blob_);
        }
    }

    const std::string& id() const override
    {
        return id_;
    }

    int64_t size_B() const override
    {
        SKAL_ASSERT(blob_ != nullptr);
        return blob_->size_B;
    }

    void ref() override
    {
        SKAL_ASSERT(blob_ != nullptr);
        ++blob_->ref;
    }

    void unref() override
    {
        SKAL_ASSERT(blob_ != nullptr);
        --blob_->ref;
        // NB: The proxy always holds a reference to the underlying blob.
        // Because this proxy object is still alive, it is impossible for the
        // reference counter to reach 0 or less in this method (unless there is
        // a bug in the upper layer, but that's not our problem).
    }

    void* map() override
    {
        blob_->check(id_);
        blob_->mutex.lock();
        // NB: The blob data starts right after the blob header
        return static_cast<void*>(blob_ + 1);
    }

    void unmap() override
    {
        blob_->check(id_);
        blob_->mutex.unlock();
    }

private :
    std::string id_;
    malloc_blob_t* blob_;
};

class malloc_allocator_t final : public blob_allocator_t
{
public :
    malloc_allocator_t() : blob_allocator_t("malloc", scope_t::process)
    {
    }

    virtual ~malloc_allocator_t() = default;

    std::unique_ptr<proxy_base_t> create(const std::string& id,
            int64_t size_B) override
    {
        (void)id; // ignored for 'malloc' allocator
        SKAL_ASSERT(size_B > 0);

        int64_t total_size_B = sizeof(malloc_blob_t) + size_B;
        void* ptr = std::malloc(total_size_B);
        SKAL_ASSERT(ptr != nullptr) << "Failed to malloc "
            << total_size_B << " bytes";

        malloc_blob_t* blob = new (ptr) malloc_blob_t(size_B);
        return std::make_unique<malloc_proxy_t>(*this, blob);
    }

    std::unique_ptr<proxy_base_t> open(const std::string& id) override
    {
        malloc_blob_t* blob = nullptr;
        if (std::sscanf(id.c_str(), "%p", &blob) != 1) {
            SKAL_LOG(error) << "Can't open 'malloc' blob id='"
                << id << "': id is not a pointer";
            throw bad_blob();
        }
        blob->check(id);
        ++blob->ref; // Incr. reference counter for proxy about to be created
        return std::make_unique<malloc_proxy_t>(*this, blob);
    }
};


// "shm" blob allocator

constexpr const char* shm_blob_magic = "sharedXX";

/** Delete a shared memory object */
void shm_remove(const std::string& id)
{
    if (!shared_memory_object::remove(id.c_str())) {
        SKAL_LOG(warning)
            << "Failed to remove shared memory area '" << id << "'";
    }
}

/** Header at the beginning of a "shm" blob */
struct shm_blob_t final
{
    char                 magic[8]; /**< Magic number */
    std::atomic<int64_t> ref;      /**< Reference counter */
    int64_t              size_B;   /**< Size initially requested */

    /** Mutex for exclusive access to the blob */
    ft::safe_mutex<interprocess_mutex> mutex;

    /** Constructor
     *
     * NB: We always assume that whoever constructed this blob holds a
     * reference to it, hence we set the `ref` counter to 1.
     */
    shm_blob_t(int64_t bytes) : ref(1), size_B(bytes), mutex()
    {
        std::memcpy(magic, shm_blob_magic, sizeof(magic));
    }

    ~shm_blob_t()
    {
        // The thread destroying a safe_mutex must not have it locked, so
        // ensure it is unlocked before destroying it.
        mutex.unlock();
    }

    /** Check the "shm" blob
     *
     * \param id [in] Blob id
     *
     * \throw `skal::error` if blob header is invalid
     */
    void check(const std::string& id)
    {
        if (this == nullptr) {
            SKAL_LOG(error) << "Invalid 'shm' blob '" << id
                << "': `this` is a nullptr";
            throw bad_blob();
        }
        if (std::memcmp(magic, shm_blob_magic, sizeof(magic)) != 0) {
            SKAL_LOG(error) << "Invalid 'shm' blob '" << id
                << "': wrong magic number";
            throw bad_blob();
        }
        if (size_B <= 0) {
            SKAL_LOG(error) << "Invalid 'shm' blob '" << id
                << "': invalid size (" << size_B << "): must be >0";
            throw bad_blob();
        }
    }
};

/** Proxy to access a "shm" blob
 *
 * Be careful not to call virtual methods inside the contructor and destructor!
 */
class shm_proxy_t final : public proxy_base_t
{
public :
    shm_proxy_t(blob_allocator_t& allocator, std::string id, int64_t size_B,
            std::unique_ptr<shared_memory_object> shm)
        : proxy_base_t(allocator)
        , id_(std::move(id))
        , size_B_(size_B)
        , shm_(std::move(shm))
    {
        SKAL_ASSERT(size_B_ > 0);
        SKAL_ASSERT(shm_);
        // NB: The blob's reference counter has been incremented for this
        // proxy by the blob allocator, so we don't do it here.
    }

    ~shm_proxy_t()
    {
        // NB: The blob is necessarily unmapped when destroyed, because the
        // lifetime of any `scoped_map_t` object created by the user of this
        // proxy is less than the proxy itself (unless the user software has a
        // bug, but that's not our problem).
        SKAL_ASSERT(shm_);
        mapped_region region(*shm_, read_write);
        void* addr = region.get_address();
        shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
        blob->check(id_);
        if (--blob->ref <= 0) {
            // NB: Because the shm blob object has been constructed with a
            // placement-new operation, we have to call the destructor
            // manually.
            blob->~shm_blob_t();
            shm_remove(id());
        }
    }

    const std::string& id() const override
    {
        return id_;
    }

    int64_t size_B() const override
    {
        return size_B_;
    }

    void ref() override
    {
        SKAL_ASSERT(region_);
        void* addr = region_->get_address();
        shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
        ++blob->ref;
    }

    void unref() override
    {
        SKAL_ASSERT(region_);
        void* addr = region_->get_address();
        shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
        --blob->ref;
        // NB: The proxy always holds a reference to the underlying blob.
        // Because this proxy object is still alive, it is impossible for the
        // reference counter to reach 0 or less in this method (unless there is
        // a bug in the upper layer, but that's not our problem).
    }

    void* map() override
    {
        SKAL_ASSERT(shm_);
        SKAL_ASSERT(!region_);

        try {
            region_.reset(new mapped_region(*shm_, read_write));
        } catch (std::exception& e) { // TODO: determine exact exception
            SKAL_LOG(error) << "Failed to map 'shm' blob '" << id_ << "': "
                << e.what();
            throw bad_blob();
        }

        void* addr = region_->get_address();
        shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
        blob->check(id_);
        blob->mutex.lock();
        // NB: The blob data starts right after the blob header
        return static_cast<void*>(blob + 1);
    }

    void unmap() override
    {
        SKAL_ASSERT(region_);
        void* addr = region_->get_address();
        shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
        blob->check(id_);
        blob->mutex.unlock();
        region_.reset();
    }

private :
    std::string id_;
    int64_t size_B_;
    std::unique_ptr<shared_memory_object> shm_;
    boost::scoped_ptr<mapped_region> region_;
};

class shm_allocator_t final : public blob_allocator_t
{
public :
    shm_allocator_t() : blob_allocator_t("shm", scope_t::computer)
    {
    }

    virtual ~shm_allocator_t() = default;

    std::unique_ptr<proxy_base_t> create(const std::string& id,
            int64_t size_B) override
    {
        SKAL_ASSERT(!id.empty());
        SKAL_ASSERT(size_B > 0);

        std::unique_ptr<shared_memory_object> shm;
        try {
            shm.reset(new shared_memory_object(create_only,
                        id.c_str(), read_write));
        } catch (std::exception& e) { // TODO: determine the exact exception
            SKAL_LOG(warning) << "Failed to create shared memory blob '"
                << id << "' because it already exists: " << e.what();
            throw bad_blob();
        }

        int64_t total_size_B = sizeof(shm_blob_t) + size_B;
        try {
            shm->truncate(total_size_B);
        } catch (std::exception& e) { // TODO: determine the exact exception
            SKAL_LOG(error) << "Failed to set size of shared memory blob '"
                << id << "' to " << total_size_B << " bytes: " << e.what();
            shm_remove(id);
            throw bad_blob();
        }

        try {
            mapped_region region(*shm, read_write);
            void* addr = region.get_address();
            new (addr) shm_blob_t(size_B);
        } catch (std::exception& e) { // TODO: determine the exact exception
            SKAL_LOG(error) << "Failed to map shared memory blob '" << id
                << "' into current address space: " << e.what();
            shm_remove(id);
            throw bad_blob();
        }

        return std::make_unique<shm_proxy_t>(*this,
                std::move(id), size_B, std::move(shm));
    }

    std::unique_ptr<proxy_base_t> open(const std::string& id) override
    {
        SKAL_ASSERT(!id.empty());

        std::unique_ptr<shared_memory_object> shm;
        try {
            shm.reset(new shared_memory_object(open_only,
                        id.c_str(), read_write));
        } catch (std::exception& e) { // TODO: determine exact exception
            SKAL_LOG(error) << "Failed to open shared memory blob '"
                << id << "' because it does not exist: " << e.what();
            throw bad_blob();
        }

        int64_t size_B = 0;
        try {
            mapped_region region(*shm, read_write);
            void* addr = region.get_address();
            shm_blob_t* blob = static_cast<shm_blob_t*>(addr);
            blob->check(id);
            ++blob->ref; // Incr. ref. counter for proxy about to be created
            size_B = blob->size_B;
        } catch (bad_blob&) {
            throw; // re-throw `bad_blob` exceptions
        } catch (std::exception& e) { // TODO: determine exact exception
            SKAL_LOG(error) << "Failed to map shared memory blob '" << id
                << "' into current address space: " << e.what();
            throw bad_blob();
        }

        return std::make_unique<shm_proxy_t>(*this,
                std::move(id), size_B, std::move(shm));
    }
};

typedef std::map<std::string, std::unique_ptr<blob_allocator_t>> registry_t;

/** Allocator registry */
registry_t g_allocators;

/** Mutex to protect allocator registry */
std::mutex g_mutex;
typedef std::unique_lock<std::mutex> lock_t;

/** Module initialisation */
struct init_t
{
    init_t()
    {
        register_allocator(std::make_unique<malloc_allocator_t>());
        register_allocator(std::make_unique<shm_allocator_t>());
    }
} g_init;

} // unnamed namespace

blob_proxy_t::blob_proxy_t(const blob_proxy_t& right)
{
    SKAL_ASSERT(!right.is_mapped_);
    base_proxy_ = right.allocator().open(right.id());
    SKAL_ASSERT(base_proxy_);
    is_mapped_ = false;
}

void blob_proxy_t::ref()
{
    if (is_mapped_) {
        base_proxy_->ref();
    } else {
        // Blob is not mapped => map it temporarily
        scoped_map_t m(*this);
        base_proxy_->ref();
    }
}

void blob_proxy_t::unref()
{
    if (is_mapped_) {
        base_proxy_->unref();
    } else {
        // Blob is not mapped => map it temporarily
        scoped_map_t m(*this);
        base_proxy_->unref();
    }
}

std::string to_string(blob_allocator_t::scope_t scope)
{
    switch (scope) {
    case blob_allocator_t::scope_t::process :
        return "process";

    case blob_allocator_t::scope_t::computer :
        return "computer";

    case blob_allocator_t::scope_t::system :
        return "system";

    default :
        return "unknown";
    }
}

void register_allocator(std::unique_ptr<blob_allocator_t> allocator)
{
    lock_t lock(g_mutex);
    SKAL_ASSERT(g_allocators.find(allocator->name()) == g_allocators.end());
    g_allocators[allocator->name()] = std::move(allocator);
}

blob_allocator_t& find_allocator(const std::string& allocator_name)
{
    lock_t lock(g_mutex);
    return *(g_allocators.at(allocator_name));
}

blob_proxy_t create_blob(const std::string& allocator_name,
        const std::string& id, int64_t size_B)
{
    return blob_proxy_t(find_allocator(allocator_name).create(id, size_B));
}

blob_proxy_t open_blob(const std::string& allocator_name,
        const std::string& id)
{
    return blob_proxy_t(find_allocator(allocator_name).open(id));
}

} // namespace skal
