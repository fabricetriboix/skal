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

static void* skalTestBlobAllocate(void* cookie, const char* id, int64_t size_B)
{
    (void)id; // unused argument
    RTT_ASSERT(cookie == (void*)0xdeadbeef);
    gBlobCount++;
    return malloc(size_B);
}

static void skalTestBlobFree(void* cookie, void* obj)
{
    RTT_ASSERT(cookie == (void*)0xdeadbeef);
    gBlobCount--;
    free(obj);
}

static void* skalTestBlobMap(void* cookie, void* obj)
{
    RTT_ASSERT(cookie == (void*)0xdeadbeef);
    return obj;
}

static void skalTestBlobUnmap(void* cookie, void* obj)
{
    (void)obj; // unused argument
    RTT_ASSERT(cookie == (void*)0xdeadbeef);
}

static SkalAllocator gSkalTestBlobAllocator = {
    "test",                 // name
    false,                  // interProcess
    skalTestBlobAllocate,   // allocate
    skalTestBlobFree,       // free
    skalTestBlobMap,        // map
    skalTestBlobUnmap,      // unmap
    (void*)0xdeadbeef       // cookie
};


static RTBool skalBlobTestGroupEntry(void)
{
    SkalBlobInit(&gSkalTestBlobAllocator, 1);
    return RTTrue;
}

static RTBool skalBlobTestGroupExit(void)
{
    SkalBlobExit();
    return RTTrue;
}

RTT_GROUP_START(TestSkalBlob, 0x00030001u,
        skalBlobTestGroupEntry, skalBlobTestGroupExit)

RTT_TEST_START(skal_should_allocate_a_blob)
{
    gBlob = SkalBlobCreate("test", "dummy", 1000);
    RTT_ASSERT(gBlob != NULL);
    RTT_ASSERT(gBlobCount == 1);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_id_should_be_correct)
{
    const char* id = SkalBlobId(gBlob);
    RTT_EXPECT(id != NULL);
    RTT_EXPECT(strcmp(id, "dummy") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_blob_size_should_be_correct)
{
    RTT_ASSERT(SkalBlobSize_B(gBlob) == 1000);
}
RTT_TEST_END

RTT_TEST_START(skal_should_free_blob)
{
    SkalBlobUnref(gBlob);
    RTT_ASSERT(gBlobCount == 0);
}
RTT_TEST_END

RTT_GROUP_END(TestSkalBlob,
        skal_should_allocate_a_blob,
        skal_blob_id_should_be_correct,
        skal_blob_size_should_be_correct,
        skal_should_free_blob)
