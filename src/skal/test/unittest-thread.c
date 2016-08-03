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

#include "skal-thread.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


RTT_GROUP_START(TestThreadInitExit, 0x00050001u, NULL, NULL)

RTT_TEST_START(skal_should_initialise_thread)
{
    // NB: This will create the "skal-master" thread
    SkalThreadInit();
    usleep(10000); // wait for 10ms to let the "skal-master" thread start
}
RTT_TEST_END

RTT_TEST_START(skal_should_deinitialise_thread)
{
    SkalThreadExit();
}
RTT_TEST_END

RTT_GROUP_END(TestThreadInitExit,
        skal_should_initialise_thread,
        skal_should_deinitialise_thread)


static RTBool testThreadEnterGroup(void)
{
    SkalThreadInit();
    return RTTrue;
}

static RTBool testThreadExitGroup(void)
{
    SkalThreadExit();
    return RTTrue;
}

RTT_GROUP_START(TestThreadSimple, 0x00050002u,
        testThreadEnterGroup, testThreadExitGroup)

static int gError = -1;
static int gResult = -1;

static bool testSimpleProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strncmp(SkalMsgType(msg), "quit", SKAL_NAME_MAX) == 0) {
        return false;
    }
    if (cookie != (void*)0xdeadbabe) {
        gError = 1;
    } else if (strncmp(SkalMsgType(msg), "ping", SKAL_NAME_MAX) != 0) {
        gError = 2;
    } else if (strncmp(SkalMsgSender(msg), "skal-external", SKAL_NAME_MAX)!=0) {
        gError = 3;
    } else if (strncmp(SkalMsgRecipient(msg), "simple", SKAL_NAME_MAX) != 0) {
        gError = 4;
    } else {
        gError = 0;
        gResult = 1;
    }
    return true;
}

RTT_TEST_START(skal_simple_should_create_thread)
{
    gError = -1;
    gResult = -1;
    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "simple");
    cfg.processMsg = testSimpleProcessMsg;
    cfg.cookie = (void*)0xdeadbabe;
    SkalThreadCreate(&cfg);
}
RTT_TEST_END

RTT_TEST_START(skal_simple_should_send_ping_msg)
{
    SkalMsg* msg = SkalMsgCreate("ping", "simple", 0, NULL);
    RTT_ASSERT(msg != NULL);
    SkalMsgSend(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_simple_should_receive_ping_msg)
{
    usleep(1000); // give time to thread to receive and deal with ping msg
    RTT_EXPECT(0 == gError);
    RTT_EXPECT(1 == gResult);
}
RTT_TEST_END

RTT_GROUP_END(TestThreadSimple,
        skal_simple_should_create_thread,
        skal_simple_should_send_ping_msg,
        skal_simple_should_receive_ping_msg)
