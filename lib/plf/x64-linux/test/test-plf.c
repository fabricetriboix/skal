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

#include "skal-plf.h"
#include "rttest.h"
#include <string.h>
#include <unistd.h>


static RTBool testPlfGroupEnter(void)
{
    SkalPlfInit();
    return RTTrue;
}

static RTBool testPlfGroupExit(void)
{
    SkalPlfExit();
    return RTTrue;
}


static SkalPlfMutex* gMutex = NULL;
static SkalPlfCondVar* gCondVar = NULL;
static bool gGoAhead = false;
static SkalPlfThread* gThread = NULL;

static char gThreadName[100] = "nothing";
static void* gThreadArg = (void*)0xcafedeca;
static void* gThreadSpecific = NULL;

static enum {
    THREAD_STARTING,
    THREAD_WAITING,
    THREAD_FINISHED
} gState = THREAD_STARTING;

static void testSkalPlfThreadFn(void* arg)
{
    SkalPlfThreadSetSpecific((void*)0xdeadbabe);
    gState = THREAD_WAITING;
    SkalPlfMutexLock(gMutex);
    while (!gGoAhead) {
        SkalPlfCondVarWait(gCondVar, gMutex);
    }
    gThreadArg = arg;
    snprintf(gThreadName, sizeof(gThreadName), "%s", SkalPlfThreadGetName());
    SkalPlfMutexUnlock(gMutex);
    gThreadSpecific = SkalPlfThreadGetSpecific();
    gState = THREAD_FINISHED;
}


RTT_GROUP_START(TestSkalPlf, 0x00010001u, testPlfGroupEnter, testPlfGroupExit)

RTT_TEST_START(skal_plf_should_create_mutex)
{
    gMutex = SkalPlfMutexCreate();
    RTT_ASSERT(gMutex != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_should_create_condvar)
{
    gCondVar = SkalPlfCondVarCreate();
    RTT_ASSERT(gCondVar != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_should_create_thread)
{
    gThread = SkalPlfThreadCreate("TestThread", testSkalPlfThreadFn,
            (void*)0xdeadbeef);
    RTT_ASSERT(gThread != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_thread_arg_should_be_unchanged_before_signal)
{
    RTT_EXPECT(((void*)0xcafedeca) == gThreadArg);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_thread_name_should_be_unchanged_before_signal)
{
    RTT_EXPECT(strcmp(gThreadName, "nothing") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_thread_arg_should_be_changed_after_signal)
{
    // Allow the thread to reach the stage where it will wait on the condvar
    while (gState != THREAD_WAITING) {
        usleep(100);
    }

    // Tell the thread to go ahead
    SkalPlfMutexLock(gMutex);
    gGoAhead = true;
    SkalPlfMutexUnlock(gMutex);
    SkalPlfCondVarSignal(gCondVar);

    // Allow the thread to run
    while (gState != THREAD_FINISHED) {
        usleep(100);
    }

    RTT_EXPECT(((void*)0xdeadbeef) == gThreadArg);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_thread_name_should_be_changed_after_signal)
{
    RTT_EXPECT(strcmp(gThreadName, "TestThread") == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_should_free_up_resources)
{
    SkalPlfThreadJoin(gThread);
    SkalPlfCondVarDestroy(gCondVar);
    SkalPlfMutexDestroy(gMutex);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_thread_specific_should_be_correct)
{
    RTT_EXPECT((void*)0xdeadbabe == gThreadSpecific);
}
RTT_TEST_END

RTT_TEST_START(skal_plf_should_generate_a_random_number)
{
    (void)SkalPlfRandomU64();
}
RTT_TEST_END

RTT_GROUP_END(TestSkalPlf,
        skal_plf_should_create_mutex,
        skal_plf_should_create_condvar,
        skal_plf_should_create_thread,
        skal_plf_thread_arg_should_be_unchanged_before_signal,
        skal_plf_thread_name_should_be_unchanged_before_signal,
        skal_plf_thread_arg_should_be_changed_after_signal,
        skal_plf_thread_name_should_be_changed_after_signal,
        skal_plf_should_free_up_resources,
        skal_plf_thread_specific_should_be_correct,
        skal_plf_should_generate_a_random_number)
