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


typedef struct {
    CdsMapItem    item;
    int           ref;
    SkalAllocator allocator;
} skalAllocatorItem;


struct SkalBlob {
    SkalAllocator* allocator;
    int            ref;
    char*          id;
    char*          name;
    int64_t        size_B;
    void*          obj;
};



/*-------------------------------+
 | Private function declarations |
 +-------------------------------*/


/** Allocator map: unreference an item
 *
 * @param litem [in,out] Allocator item to unreference
 */
static void skalAllocatorMapUnref(CdsMapItem* litem);


/** Register an allocator into the allocator map
 *
 * @param allocator [in] Description of allocator to register
 */
static void skalRegisterAllocator(const SkalAllocator* allocator);


/** "malloc" allocator: allocate a memory area
 *
 * @param cookie [in] Unused
 * @param id     [in] Unused
 * @param size_B [in] Number of bytes to allocate, must be >0
 *
 * @return The allocated memory area; this function never returns NULL
 */
static void* skalMallocAllocate(void* cookie, const char* id, int64_t size_B);


/** "malloc" allocator: free a memory area
 *
 * @param cookie [in]     Unused
 * @param obj    [in,out] Memory area to de-allocate
 */
static void skalMallocDeallocate(void* cookie, void* obj);


/** "malloc" allocator: map a memory area into process memory space
 *
 * @param cookie [in]     Unused
 * @param obj    [in,out] Memory area to map
 *
 * @return Mapped memory area (same as `obj` actually)
 */
static void* skalMallocMap(void* cookie, void* obj);


/** "malloc" allocator: unmap from process memory space
 *
 * @param cookie [in]     Unused
 * @param obj    [in,out] Memory area to unmap
 */
static void skalMallocUnmap(void* cookie, void* obj);


// TODO: Implemented "shm" allocator
/** "shm" allocator: allocate a memory area
 */
static void* skalShmAllocate(void* cookie, const char* id, int64_t size_B);

static void skalShmDeallocate(void* cookie, void* obj);

static void* skalShmMap(void* cookie, void* obj);

static void skalShmUnmap(void* cookie, void* obj);



/*------------------+
 | Global variables |
 +------------------*/


/** Map of allocators
 *
 * NB: We don't bother protecting it, because we assume it will not be modified
 * after initialisation.
 */
static CdsMap* gAllocatorMap = NULL;



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


void SkalBlobInit(const SkalAllocator* allocators, int size)
{
    if (size > 0) {
        SKALASSERT(allocators != NULL);
    }

    SKALASSERT(gAllocatorMap == NULL);
    gAllocatorMap = CdsMapCreate("SkalAllocators", SKAL_ALLOCATORS_MAX,
            SkalStringCompare, NULL, NULL, skalAllocatorMapUnref);

    SkalAllocator mallocAllocator = {
        "malloc",                     // name
        SKAL_ALLOCATOR_SCOPE_PROCESS, // scope
        skalMallocAllocate,           // allocate
        skalMallocDeallocate,         // deallocate
        skalMallocMap,                // map
        skalMallocUnmap,              // unmap
        NULL                          // cookie
    };
    skalRegisterAllocator(&mallocAllocator);

    SkalAllocator shmAllocator = {
        "shm",                         // name
        SKAL_ALLOCATOR_SCOPE_COMPUTER, // scope
        skalShmAllocate,               // allocate
        skalShmDeallocate,             // deallocate
        skalShmMap,                    // map
        skalShmUnmap,                  // unmap
        NULL                           // cookie
    };
    skalRegisterAllocator(&shmAllocator);

    for (int i = 0; i < size; i++) {
        skalRegisterAllocator(&allocators[i]);
    }
}


void SkalBlobExit(void)
{
    SKALASSERT(gAllocatorMap != NULL);
    CdsMapDestroy(gAllocatorMap);
}


SkalBlob* SkalBlobCreate(const char* allocator, const char* id,
        const char* name, int64_t size_B)
{
    if ((NULL == allocator) || (strlen(allocator) == 0)) {
        allocator = "malloc";
    }
    SKALASSERT(gAllocatorMap != NULL);

    SkalBlob* blob = NULL;
    skalAllocatorItem* allocatorItem = (skalAllocatorItem*)CdsMapSearch(
            gAllocatorMap, (void*)allocator);
    if (allocatorItem != NULL) {
        SKALASSERT(allocatorItem->allocator.allocate != NULL);
        void* obj = allocatorItem->allocator.allocate(
                allocatorItem->allocator.cookie, id, size_B);
        if (obj != NULL) {
            blob = SkalMallocZ(sizeof(*blob));
            // NB: The map of allocators stay static after initialisation
            blob->allocator = &allocatorItem->allocator;
            blob->ref = 1;
            if (id != NULL) {
                blob->id = SkalStrdup(id);
            }
            if (name != NULL) {
                blob->name = SkalStrdup(name);
            }
            blob->size_B = size_B;
            blob->obj = obj;
        }
    }
    return blob;
}


void SkalBlobRef(SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    blob->ref++;
}


void SkalBlobUnref(SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    blob->ref--;
    if (blob->ref <= 0) {
        SKALASSERT(blob->allocator->deallocate != NULL);
        blob->allocator->deallocate(blob->allocator->cookie, blob->obj);
        free(blob->id);
        free(blob->name);
        free(blob);
    }
}


void* SkalBlobMap(SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    SKALASSERT(blob->allocator->map != NULL);
    return blob->allocator->map(blob->allocator->cookie, blob->obj);
}


void SkalBlobUnmap(SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    SKALASSERT(blob->allocator->unmap != NULL);
    return blob->allocator->unmap(blob->allocator->cookie, blob->obj);
}


const char* SkalBlobId(const SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    return blob->id;
}


const char* SkalBlobName(const SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    return blob->name;
}


int64_t SkalBlobSize_B(const SkalBlob* blob)
{
    SKALASSERT(blob != NULL);
    return blob->size_B;
}



/*----------------------------------+
 | Private function implementations |
 +----------------------------------*/


static void skalAllocatorMapUnref(CdsMapItem* litem)
{
    skalAllocatorItem* item = (skalAllocatorItem*)litem;
    item->ref--;
    if (item->ref <= 0) {
        free(item->allocator.name);
        free(item);
    }
}


static void skalRegisterAllocator(const SkalAllocator* allocator)
{
    SKALASSERT(gAllocatorMap != NULL);
    SKALASSERT(allocator != NULL);
    SKALASSERT(allocator->name != NULL);
    SKALASSERT(allocator->allocate != NULL);
    SKALASSERT(allocator->deallocate != NULL);
    SKALASSERT(allocator->map != NULL);
    SKALASSERT(allocator->unmap != NULL);

    skalAllocatorItem* item = SkalMallocZ(sizeof(*item));
    item->ref = 1;
    item->allocator = *allocator;
    // NB: Do not use the caller's storage space as its lifetime is unknown
    item->allocator.name = SkalStrdup(allocator->name);

    // NB: If 2 allocators with the same names are inserted, the last one will
    // "overwrite" the previous one. This is intended.
    SKALASSERT(CdsMapInsert(gAllocatorMap,
                (void*)(item->allocator.name), &item->item));
}


static void* skalMallocAllocate(void* cookie, const char* id, int64_t size_B)
{
    SKALASSERT(size_B > 0);
    return SkalMalloc(size_B);
}


static void skalMallocDeallocate(void* cookie, void* obj)
{
    free(obj);
}


static void* skalMallocMap(void* cookie, void* obj)
{
    return obj;
}


static void skalMallocUnmap(void* cookie, void* obj)
{
}


static void* skalShmAllocate(void* cookie, const char* id, int64_t size_B)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmDeallocate(void* cookie, void* obj)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void* skalShmMap(void* cookie, void* obj)
{
    SKALPANIC_MSG("Not yet implemented");
}


static void skalShmUnmap(void* cookie, void* obj)
{
    SKALPANIC_MSG("Not yet implemented");
}
