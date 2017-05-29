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


static int   gBlobCount = 0;
static void* gBlob = NULL;

static SkalBlob* skalTestBlobCreate(void* cookie,
        const char* id, int64_t size_B)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    gBlobCount++;
    return malloc(sizeof(SkalBlob) + size_B);
}

static SkalBlob* skalTestBlobOpen(void* cookie, const char* id)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    SkalBlob* blob;
    int ret = sscanf(id, "%p", &blob);
    SKALASSERT(1 == ret);
    return blob;
}

static void skalTestBlobRef(void* cookie, SkalBlob* blob)
{
}

static void skalTestBlobUnref(void* cookie, SkalBlob* blob)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    free(blob);
    gBlobCount--;
}

static uint8_t* skalTestBlobMap(void* cookie, SkalBlob* blob)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    uint8_t* ptr = (uint8_t*)blob;
    return ptr + sizeof(SkalBlob);
}

static void skalTestBlobUnmap(void* cookie, SkalBlob* blob)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
}

static const char* skalTestBlobId(void* cookie, const SkalBlob* blob)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    static char id[64];
    snprintf(id, 64, "%p", blob);
    return id;
}

static int64_t skalTestBlobSize(void* cookie, const SkalBlob* blob)
{
    SKALASSERT(cookie == (void*)0xdeadbeef);
    return 1;
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
    gBlob = SkalBlobCreate("test", NULL, 1000);
    RTT_ASSERT(gBlob != NULL);
    RTT_ASSERT(gBlobCount == 1);
}
RTT_TEST_END

static char gId[128];

RTT_TEST_START(skal_blob_id_should_be_correct)
{
    const char* id = SkalBlobId(gBlob);
    RTT_ASSERT(id != NULL);
    snprintf(gId, sizeof(gId), "%p", gBlob);
    RTT_EXPECT(SkalStrcmp(id, gId) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_should_open_blob)
{
    SkalBlob* blob = SkalBlobOpen("test", gId);
    RTT_EXPECT(blob != NULL);
    RTT_EXPECT(blob == gBlob);
}
RTT_TEST_END

RTT_TEST_START(skal_should_free_blob)
{
    SkalBlobUnref(gBlob);
    RTT_ASSERT(gBlobCount == 0);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalBlob,
        skal_should_create_a_blob,
        skal_blob_id_should_be_correct,
        skal_should_open_blob,
        skal_should_free_blob)
