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

#include "skal-blob.h"
#include <stdlib.h>
#include <string.h>
#include "cdsmap.h"



/*----------------+
 | Macros & Types |
 +----------------*/


#define SKAL_MALLOC_BLOB_MAGIC "mallocX"


/** Structure at the beginning of a "malloc" blob */
typedef struct {
    char    magic[8];    /**< Magic number */
    int64_t ref;         /**< Reference counter */
    int64_t size_B;      /**< Number of bytes initially requested */
    int64_t totalSize_B; /**< Total size, including header */
} skalMallocBlobHeader;


/** Proxy to access a "malloc" blob */
typedef struct {
    SkalBlobProxy         parent;
    char                  id[(sizeof(void*)*2) + 3];
    skalMallocBlobHeader* hdr; /**< Pointer to actual blob */
} skalMallocBlobProxy;



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Allocator map: unreference an item
 *
 * NB: Because allocators are only referenced once (in the `gAllocatorMap`), we
 * don't keep track of their reference counters.
 *
 * @param item [in,out] Allocator item to unreference
 */
static void skalAllocatorMapUnref(CdsMapItem* item);


/** Register an allocator into the allocator map
 *
 * @param allocator [in] Description of allocator to register
 */
static void skalRegisterAllocator(const SkalAllocator* allocator);


/** "malloc" allocator: Create a new blob
 *
 * The new blob will have its reference counter initialised to 1.
 *
 * @param cookie [in] Unused
 * @param id     [in] Unused
 * @param size_B [in] Number of bytes to allocate, must be >0
 *
 * @return A proxy to the newly created blob; this function never returns NULL
 */
static SkalBlobProxy* skalMallocCreate(void* cookie,
        const char* id, int64_t size_B);


/** "malloc" allocator: Open an existing blob
 *
 * The blob's reference counter will be incremented.
 *
 * @param cookie [in] Unused
 * @param id     [in] Hex representation of the address of the first byte of the
 *                    blob; must not be NULL
 *
 * @return A proxy to the opened blob, or NULL if `id` is not valid
 */
static SkalBlobProxy* skalMallocOpen(void* cookie, const char* id);


/** "malloc" allocator: Close a blob proxy
 *
 * The blob's reference counter will be decremented. The blob will be
 * de-allocated if it was the last reference.
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob proxy to close; must not be NULL
 */
static void skalMallocClose(void* cookie, SkalBlobProxy* blob);


/** "malloc" allocator: Add a reference to a blob
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to reference; must not be NULL
 */
static void skalMallocRef(void* cookie, SkalBlobProxy* blob);


/** "malloc" allocator: Remove a reference to a blob
 *
 * The blob will be de-allocated when the last reference to it is removed.
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to unreference; must not be NULL
 */
static void skalMallocUnref(void* cookie, SkalBlobProxy* blob);


/** "malloc" allocator: Map a blob into process memory space
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to map; must not be NULL
 *
 * @return Mapped memory area
 */
static uint8_t* skalMallocMap(void* cookie, SkalBlobProxy* blob);


/** "malloc" allocator: Unmap from process memory space
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to unmap; must not be NULL
 */
static void skalMallocUnmap(void* cookie, SkalBlobProxy* blob);


/** "malloc" allocator: Get the blob's id
 *
 * @param cookie [in] Unused
 * @param blob   [in] Blob to query; must not be NULL
 *
 * @return The blob's id, which is a hex representation of its address
 */
static const char* skalMallocBlobId(void* cookie, const SkalBlobProxy* blob);


/** "malloc" allocator: Get the blob's size, in bytes
 *
 * @param cookie [in] Unused
 * @param blob   [in] Blob to query; must not be NULL
 *
 * @return The blob's size, in bytes
 */
static int64_t skalMallocBlobSize(void* cookie, const SkalBlobProxy* blob);


/** "shm" allocator: Create a new blob
 *
 * The new blob will have its reference counter initialised to 1.
 *
 * @param cookie [in] Unused
 * @param id     [in] Name of shared memory area to create
 * @param size_B [in] Minimum number of bytes to allocate, must be >0
 *
 * @return A blob proxy to the newly created blob; this function returns NULL if
 *         `id` refers to an existing shared memory area; this function asserts
 *         for all other errors
 */
static SkalBlobProxy* skalShmCreate(void* cookie,
        const char* id, int64_t size_B);


/** "shm" allocator: Open an existing blob
 *
 * The blob's reference counter will be incremented.
 *
 * @param cookie [in] Unused
 * @param id     [in] Name of shared memory area to open
 *
 * @return A proxy to the opened blob, or NULL if `id` does not exist
 */
static SkalBlobProxy* skalShmOpen(void* cookie, const char* id);


/** "shm" allocator: Close a blob proxy
 *
 * The blob's reference counter will be decremented. The blob will be
 * de-allocated if it was the last reference.
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob proxy to close; must not be NULL
 */
static void skalShmClose(void* cookie, SkalBlobProxy* proxy);


/** "shm" allocator: Add a reference to a blob
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to reference; must not be NULL
 */
static void skalShmRef(void* cookie, SkalBlobProxy* blob);


/** "shm" allocator: Remove a reference to a blob
 *
 * The blob will be de-allocated when the last reference to it is removed.
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to unreference; must not be NULL
 */
static void skalShmUnref(void* cookie, SkalBlobProxy* blob);


/** "shm" allocator: Map a blob into process memory space
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to map; must not be NULL
 *
 * @return Mapped memory area
 */
static uint8_t* skalShmMap(void* cookie, SkalBlobProxy* blob);


/** "shm" allocator: Unmap a blob from process memory space
 *
 * @param cookie [in]     Unused
 * @param blob   [in,out] Blob to unmap; must not be NULL
 */
static void skalShmUnmap(void* cookie, SkalBlobProxy* blob);


/** "shm" allocator: Get a blob id
 *
 * @param cookie [in] Unused
 * @param blob   [in] Blob to query; must not be NULL
 *
 * @return The blob id, which is the name of the shared memory area
 */
static const char* skalShmBlobId(void* cookie, const SkalBlobProxy* blob);


/** "shm" allocator: Get the blob size, in bytes
 *
 * @param cookie [in] Unused
 * @param blob   [in] Blob to query; must not be NULL
 *
 * @return The blob size, in bytes
 */
static int64_t skalShmBlobSize(void* cookie, const SkalBlobProxy* blob);



/*------------------+
 | Global variables |
 +------------------*/


/** Map of allocators
 *
 * NB: We don't bother protecting it, because we assume it will not be modified
 * after initialisation.
 */
static CdsMap* gAllocatorMap = NULL;


/** Number of blob references in this process */
static int64_t gBlobRefCount_DEBUG = 0;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalBlobInit(const SkalAllocator* allocators, int size)
{
    if (size > 0) {
        SKALASSERT(allocators != NULL);
    }

    SKALASSERT(gAllocatorMap == NULL);
    gAllocatorMap = CdsMapCreate("SkalAllocators", 0,
            SkalStringCompare, NULL, NULL, skalAllocatorMapUnref);

    SkalAllocator mallocAllocator;
    memset(&mallocAllocator, 0, sizeof(mallocAllocator));
    mallocAllocator.name = "malloc";
    mallocAllocator.scope = SKAL_ALLOCATOR_SCOPE_PROCESS;
    mallocAllocator.create = skalMallocCreate;
    mallocAllocator.open = skalMallocOpen;
    mallocAllocator.close = skalMallocClose;
    mallocAllocator.ref = skalMallocRef;
    mallocAllocator.unref = skalMallocUnref;
    mallocAllocator.map = skalMallocMap;
    mallocAllocator.unmap = skalMallocUnmap;
    mallocAllocator.blobid = skalMallocBlobId;
    mallocAllocator.blobsize = skalMallocBlobSize;
    skalRegisterAllocator(&mallocAllocator);

    SkalAllocator shmAllocator;
    memset(&shmAllocator, 0, sizeof(shmAllocator));
    shmAllocator.name = "shm";
    shmAllocator.scope = SKAL_ALLOCATOR_SCOPE_COMPUTER;
    shmAllocator.create = skalShmCreate;
    shmAllocator.open = skalShmOpen;
    shmAllocator.close = skalShmClose;
    shmAllocator.ref = skalShmRef;
    shmAllocator.unref = skalShmUnref;
    shmAllocator.map = skalShmMap;
    shmAllocator.unmap = skalShmUnmap;
    shmAllocator.blobid = skalShmBlobId;
    shmAllocator.blobsize = skalShmBlobSize;
    skalRegisterAllocator(&shmAllocator);

    for (int i = 0; i < size; i++) {
        skalRegisterAllocator(&allocators[i]);
    }
}


void SkalBlobExit(void)
{
    SKALASSERT(gAllocatorMap != NULL);
    CdsMapDestroy(gAllocatorMap);
    gAllocatorMap = NULL;
}


SkalBlobProxy* SkalBlobCreate(const char* allocatorName,
        const char* id, int64_t size_B)
{
    SKALASSERT(gAllocatorMap != NULL);
    if (SkalStrlen(allocatorName) <= 0) {
        allocatorName = "malloc";
    }

    SkalBlobProxy* blob = NULL;
    SkalAllocator* allocator = (SkalAllocator*)CdsMapSearch(
            gAllocatorMap, (void*)allocatorName);
    if (allocator != NULL) {
        SKALASSERT(allocator->create != NULL);
        blob = allocator->create(allocator->cookie, id, size_B);
        if (blob != NULL) {
            blob->allocator = allocator;
            gBlobRefCount_DEBUG++;
        }
    }
    return blob;
}


SkalBlobProxy* SkalBlobOpen(const char* allocatorName, const char* id)
{
    SKALASSERT(gAllocatorMap != NULL);
    if (SkalStrlen(allocatorName) <= 0) {
        allocatorName = "malloc";
    }

    SkalBlobProxy* blob = NULL;
    SkalAllocator* allocator = (SkalAllocator*)CdsMapSearch(
            gAllocatorMap, (void*)allocatorName);
    if (allocator != NULL) {
        SKALASSERT(allocator->open != NULL);
        blob = allocator->open(allocator->cookie, id);
        if (blob != NULL) {
            blob->allocator = allocator;
            gBlobRefCount_DEBUG++;
        }
    }
    return blob;
}


void SkalBlobClose(SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->close != NULL);
    allocator->close(allocator->cookie, blob);
    gBlobRefCount_DEBUG--;
}


void SkalBlobRef(SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->ref != NULL);
    allocator->ref(allocator->cookie, blob);
    gBlobRefCount_DEBUG++;
}


void SkalBlobUnref(SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->unref != NULL);
    allocator->unref(allocator->cookie, blob);
    gBlobRefCount_DEBUG--;
}


uint8_t* SkalBlobMap(SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->map != NULL);
    return allocator->map(allocator->cookie, blob);
}


void SkalBlobUnmap(SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->unmap != NULL);
    allocator->unmap(allocator->cookie, blob);
}


const char* SkalBlobId(const SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->blobid != NULL);
    return allocator->blobid(allocator->cookie, blob);
}


int64_t SkalBlobSize_B(const SkalBlobProxy* blob)
{
    SkalAllocator* allocator = SkalBlobAllocator(blob);
    SKALASSERT(allocator->blobsize != NULL);
    return allocator->blobsize(allocator->cookie, blob);
}


SkalAllocator* SkalBlobAllocator(const SkalBlobProxy* blob)
{
    SKALASSERT(blob != NULL);
    SkalAllocator* allocator = blob->allocator;
    SKALASSERT(allocator != NULL);
    return allocator;
}


int64_t SkalBlobRefCount_DEBUG(void)
{
    return gBlobRefCount_DEBUG;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skalAllocatorMapUnref(CdsMapItem* item)
{
    SkalAllocator* allocator = (SkalAllocator*)item;
    free(allocator->name);
    free(allocator);
}


static void skalRegisterAllocator(const SkalAllocator* allocator)
{
    SKALASSERT(gAllocatorMap != NULL);
    SKALASSERT(allocator != NULL);
    SKALASSERT(SkalIsAsciiString(allocator->name));
    SKALASSERT(strchr(allocator->name, ':') == NULL);
    SKALASSERT(allocator->create != NULL);
    SKALASSERT(allocator->open != NULL);
    SKALASSERT(allocator->close != NULL);
    SKALASSERT(allocator->ref != NULL);
    SKALASSERT(allocator->unref != NULL);
    SKALASSERT(allocator->map != NULL);
    SKALASSERT(allocator->unmap != NULL);
    SKALASSERT(allocator->blobid != NULL);
    SKALASSERT(allocator->blobsize != NULL);

    SkalAllocator* copy = SkalMallocZ(sizeof(*copy));
    *copy = *allocator;
    // NB: Do not use the caller's storage space as its lifetime is unknown
    copy->name = SkalStrdup(allocator->name);

    // NB: If 2 allocators with the same names are inserted, the last one will
    // "overwrite" the previous one. This is intended.
    bool inserted = CdsMapInsert(gAllocatorMap, copy->name, (CdsMapItem*)copy);
    SKALASSERT(inserted);
}


static SkalBlobProxy* skalMallocCreate(void* cookie,
        const char* id, int64_t size_B)
{
    SKALASSERT(size_B > 0);

    int64_t totalSize_B = sizeof(skalMallocBlobHeader) + size_B;
    skalMallocBlobHeader* hdr = SkalMalloc(totalSize_B);
    int n = snprintf(hdr->magic, sizeof(hdr->magic),
            "%s", SKAL_MALLOC_BLOB_MAGIC);
    SKALASSERT(n < (int)sizeof(hdr->magic));
    hdr->ref = 1;
    hdr->size_B = size_B;
    hdr->totalSize_B = totalSize_B;

    skalMallocBlobProxy* blob = SkalMallocZ(sizeof(*blob));
    n = snprintf(blob->id, sizeof(blob->id), "%p", hdr);
    SKALASSERT(n < (int)sizeof(blob->id));
    blob->hdr = hdr;
    return (SkalBlobProxy*)blob;
}


static SkalBlobProxy* skalMallocOpen(void* cookie, const char* id)
{
    SKALASSERT(id != NULL);

    skalMallocBlobHeader* hdr = NULL;
    if (sscanf(id, "%p", &hdr) != 1) {
        return NULL;
    }
    if (SkalStrcmp(hdr->magic, SKAL_MALLOC_BLOB_MAGIC) != 0) {
        SkalLog("BLOB: malloc blob with bad magic number; this is very bad, please investigate");
        return NULL;
    }

    skalMallocBlobProxy* blob = SkalMallocZ(sizeof(*blob));
    int n = snprintf(blob->id, sizeof(blob->id), "%p", hdr);
    SKALASSERT(n < (int)sizeof(blob->id));
    blob->hdr = hdr;
    (hdr->ref)++;
    return (SkalBlobProxy*)blob;
}


static void skalMallocClose(void* cookie, SkalBlobProxy* blob)
{
    skalMallocUnref(cookie, blob);
    free(blob);
}


static void skalMallocRef(void* cookie, SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
    skalMallocBlobProxy* blob = (skalMallocBlobProxy*)sblob;
    SKALASSERT(blob->hdr != NULL);
    (blob->hdr->ref)++;
}


static void skalMallocUnref(void* cookie, SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
    skalMallocBlobProxy* blob = (skalMallocBlobProxy*)sblob;
    SKALASSERT(blob->hdr != NULL);
    (blob->hdr->ref)--;
    if (blob->hdr->ref <= 0) {
        free(blob->hdr);
        blob->hdr = NULL;
    }
}


static uint8_t* skalMallocMap(void* cookie, SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
    skalMallocBlobProxy* blob = (skalMallocBlobProxy*)sblob;
    SKALASSERT(blob->hdr != NULL);
    uint8_t* ptr = (uint8_t*)blob->hdr;
    return ptr + sizeof(skalMallocBlobHeader);
}


static void skalMallocUnmap(void* cookie, SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
}


static const char* skalMallocBlobId(void* cookie, const SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
    skalMallocBlobProxy* blob = (skalMallocBlobProxy*)sblob;
    return blob->id;
}


static int64_t skalMallocBlobSize(void* cookie, const SkalBlobProxy* sblob)
{
    SKALASSERT(sblob != NULL);
    skalMallocBlobProxy* blob = (skalMallocBlobProxy*)sblob;
    SKALASSERT(blob->hdr != NULL);
    return blob->hdr->size_B;
}


static SkalBlobProxy* skalShmCreate(void* cookie,
        const char* id, int64_t size_B)
{
    SKALPANIC_MSG("Not yet implemented");
}


static SkalBlobProxy* skalShmOpen(void* cookie, const char* id)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmClose(void* cookie, SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmRef(void* cookie, SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmUnref(void* cookie, SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static uint8_t* skalShmMap(void* cookie, SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmUnmap(void* cookie, SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static const char* skalShmBlobId(void* cookie, const SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}


static int64_t skalShmBlobSize(void* cookie, const SkalBlobProxy* blob)
{
    SKALPANIC_MSG("Not yet implemented");
}
