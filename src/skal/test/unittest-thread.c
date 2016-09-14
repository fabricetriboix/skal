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


static RTBool testThreadEnterGroup(void)
{
    SkalPlfInit();
    SkalThreadInit();
    return RTTrue;
}

static RTBool testThreadExitGroup(void)
{
    SkalThreadExit();
    SkalPlfExit();
    return RTTrue;
}


RTT_GROUP_START(TestThreadSimple, 0x00060001u,
        testThreadEnterGroup, testThreadExitGroup)

static int gError = -1;
static int gResult = -1;

static bool testSimpleProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgType(msg), "quit") == 0) {
        return false;
    }
    if (cookie != (void*)0xdeadbabe) {
        gError = 1;
    } else if (strcmp(SkalMsgType(msg), "ping") != 0) {
        gError = 2;
    } else if (strcmp(SkalMsgSender(msg), "skal-external")!=0) {
        gError = 3;
    } else if (strcmp(SkalMsgRecipient(msg), "simple") != 0) {
        gError = 4;
    } else {
        gError = 0;
        gResult = 1;
    }
    usleep(1);
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


RTT_GROUP_START(TestThreadStress, 0x00060002u,
        testThreadEnterGroup, testThreadExitGroup)

static int gMsgSend = 0;
static int gMsgRecv = 0;

static bool testReceiverProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgType(msg), "ping") == 0) {
        int64_t count = SkalMsgGetInt(msg, "count");
        if (count != (int64_t)gMsgRecv) {
            gError++;
        }
        gMsgRecv++;
    }
    usleep(1);
    return true;
}

static bool testStufferProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgType(msg), "kick") == 0) {
        SkalMsg* msg2 = SkalMsgCreate("ping", "receiver", 0, NULL);
        SkalMsgAddInt(msg2, "count", gMsgSend);
        SkalMsgSend(msg2);
        gMsgSend++;

        if (gMsgSend < 100) {
            // Send a message to myself to keep going
            SkalMsg* msg3 = SkalMsgCreate("kick", "stuffer", 0, NULL);
            SkalMsgSend(msg3);
        }
    }
    return true;
}

RTT_TEST_START(skal_stress_should_create_threads)
{
    gError = 0;

    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "receiver");
    cfg.processMsg = testReceiverProcessMsg;
    cfg.queueThreshold = 5;
    SkalThreadCreate(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "stuffer");
    cfg.processMsg = testStufferProcessMsg;
    SkalThreadCreate(&cfg);
}
RTT_TEST_END

RTT_TEST_START(skal_stress_kick_off)
{
    SkalMsg* msg = SkalMsgCreate("kick", "stuffer", 0, NULL);
    SkalMsgSend(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_stress_should_have_sent_and_recv_100_msg)
{
    usleep(50000); // give enough time to send and process 100 messages
    RTT_EXPECT(100 == gMsgSend);
    RTT_EXPECT(100 == gMsgRecv);
    RTT_EXPECT(0 == gError);
}
RTT_TEST_END

RTT_GROUP_END(TestThreadStress,
        skal_stress_should_create_threads,
        skal_stress_kick_off,
        skal_stress_should_have_sent_and_recv_100_msg)
