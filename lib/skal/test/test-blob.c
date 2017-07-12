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
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


static SkalBlobProxy* gProxy1 = NULL;
static SkalBlobProxy* gProxy2 = NULL;

typedef struct {
    SkalBlobProxy parent;
    int ref;
    int* ptr;
} blobProxy;

static SkalBlobProxy* skalTestBlobCreate(void* cookie,
        const char* id, int64_t size_B)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    blobProxy* proxy = SkalMallocZ(sizeof(*proxy));
    proxy->ref = 1;
    int* ptr = SkalMalloc(size_B + sizeof(int));
    *ptr = 1;
    proxy->ptr = ptr;
    return (SkalBlobProxy*)proxy;
}

static SkalBlobProxy* skalTestBlobOpen(void* cookie, const char* id)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    int* ptr;
    int ret = sscanf(id, "%p", &ptr);
    SKALASSERT(1 == ret);
    blobProxy* proxy = SkalMallocZ(sizeof(*proxy));
    proxy->ref = 1;
    proxy->ptr = ptr;
    (*ptr)++;
    return (SkalBlobProxy*)proxy;
}

static void skalTestBlobRefProxy(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    (proxy->ref)++;
}

static void skalTestBlobUnrefProxy(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    int* ptr = proxy->ptr;
    SKALASSERT(ptr != NULL);
    (*ptr)--;
    if (*ptr <= 0) {
        free(ptr);
    }
    free(proxy);
}

static void skalTestBlobRef(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    int* ptr = proxy->ptr;
    SKALASSERT(ptr != NULL);
    (*ptr)--;
}

static void skalTestBlobUnref(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    int* ptr = proxy->ptr;
    SKALASSERT(ptr != NULL);
    (*ptr)--;
    if (*ptr <= 0) {
        free(ptr);
        proxy->ptr = NULL;
    }
}

static uint8_t* skalTestBlobMap(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    uint8_t* ptr = (uint8_t*)(proxy->ptr);
    SKALASSERT(ptr != NULL);
    return ptr + sizeof(int);
}

static void skalTestBlobUnmap(void* cookie, SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
}

static const char* skalTestBlobId(void* cookie, const SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    blobProxy* proxy = (blobProxy*)sproxy;
    static char id[64];
    snprintf(id, sizeof(id), "%p", proxy->ptr);
    return id;
}

static int64_t skalTestBlobSize(void* cookie, const SkalBlobProxy* sproxy)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SKALASSERT(sproxy != NULL);
    return 100;
}

static RTBool skalBlobTestGroupEntry(void)
{
    SkalPlfInit();
    SkalAllocator test;
    memset(&test, 0, sizeof(test));
    test.name = "test";
    test.scope = SKAL_ALLOCATOR_SCOPE_PROCESS;
    test.create = skalTestBlobCreate;
    test.open = skalTestBlobOpen;
    test.refProxy = skalTestBlobRefProxy;
    test.unrefProxy = skalTestBlobUnrefProxy;
    test.ref = skalTestBlobRef;
    test.unref = skalTestBlobUnref;
    test.map = skalTestBlobMap;
    test.unmap = skalTestBlobUnmap;
    test.blobid = skalTestBlobId;
    test.blobsize = skalTestBlobSize;
    test.cookie = (void*)0xdeadbeef;
    SkalBlobInit(&test, 1);
    return RTTrue;
}

static RTBool skalBlobTestGroupExit(void)
{
    SkalBlobExit();
    SkalPlfExit();
    return RTTrue;
}

RTT_GROUP_START(TestSkalBlob, 0x00030001u,
        skalBlobTestGroupEntry, skalBlobTestGroupExit)

RTT_TEST_START(skal_should_create_a_blob)
{
    gProxy1 = SkalBlobCreate("test", NULL, 1000);
    RTT_ASSERT(gProxy1 != NULL);
}
RTT_TEST_END

static char gId[128];

RTT_TEST_START(skal_blob_id_should_be_correct)
{
    const char* id = SkalBlobId(gProxy1);
    RTT_ASSERT(id != NULL);
    snprintf(gId, sizeof(gId), "%s", id);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_ref_count_should_be_one)
{
    RTT_EXPECT(SkalBlobRefCount_DEBUG() == 1);
}
RTT_TEST_END

RTT_TEST_START(skal_should_open_blob)
{
    gProxy2 = SkalBlobOpen("test", gId);
    RTT_EXPECT(gProxy2 != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_ref_count_should_be_two)
{
    RTT_EXPECT(SkalBlobRefCount_DEBUG() == 2);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_should_unref_proxy1)
{
    SkalBlobProxyUnref(gProxy1);
    gProxy1 = NULL;
    RTT_EXPECT(SkalBlobRefCount_DEBUG() == 1);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_should_have_correct_allocator)
{
    SkalAllocator* allocator = SkalBlobAllocator(gProxy2);
    RTT_EXPECT(allocator != NULL);
    RTT_EXPECT(SkalStrcmp(allocator->name, "test") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_should_unref_proxy2)
{
    SkalBlobProxyUnref(gProxy2);
    gProxy2 = NULL;
}
RTT_TEST_END

RTT_TEST_START(skal_blob_ref_count_should_be_zero)
{
    RTT_EXPECT(SkalBlobRefCount_DEBUG() == 0);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalBlob,
        skal_should_create_a_blob,
        skal_blob_id_should_be_correct,
        skal_blob_ref_count_should_be_one,
        skal_should_open_blob,
        skal_blob_ref_count_should_be_two,
        skal_blob_should_unref_proxy1,
        skal_blob_should_have_correct_allocator,
        skal_blob_should_unref_proxy2,
        skal_blob_ref_count_should_be_zero)


RTT_GROUP_START(TestMallocBlob, 0x00030002u,
        skalBlobTestGroupEntry, skalBlobTestGroupExit)

RTT_TEST_START(skal_malloc_should_create_blob)
{
    gProxy1 = SkalBlobCreate(NULL, NULL, 500);
    RTT_ASSERT(gProxy1 != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_malloc_should_map_blob)
{
    uint8_t* ptr = SkalBlobMap(gProxy1);
    RTT_EXPECT(ptr != NULL);
    strcpy((char*)ptr, "Hello, World!");
    SkalBlobUnmap(gProxy1);
}
RTT_TEST_END

RTT_TEST_START(skal_malloc_blob_should_have_correct_content)
{
    uint8_t* ptr = SkalBlobMap(gProxy1);
    RTT_EXPECT(SkalStrcmp((char*)ptr, "Hello, World!") == 0);
    SkalBlobUnmap(gProxy1);
}
RTT_TEST_END

RTT_TEST_START(skal_malloc_should_unref_proxy)
{
    SkalBlobProxyUnref(gProxy1);
}
RTT_TEST_END

RTT_GROUP_END(TestMallocBlob,
        skal_malloc_should_create_blob,
        skal_malloc_should_map_blob,
        skal_malloc_blob_should_have_correct_content,
        skal_malloc_should_unref_proxy)
