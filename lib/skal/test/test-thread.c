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

#include "skal-thread.h"
#include "skal-net.h"
#include "skal-msg.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define SOCKPATH "pseudo-skald.sock"

static SkalNet* gNet = NULL;
static int gServerSockid = -1;
static int gClientSockid = -1;
static bool gHasConnected = false;
static SkalPlfThread* gPseudoSkaldThread = NULL;
static int gSockidPipeServer = -1;
static int gSockidPipeClient = -1;

static void pseudoSkald(void* arg)
{
    (void)arg;
    SkalPlfThreadSetSpecific((void*)0xcafedeca); // to fool skal-msg
    bool stop = false;
    while (!stop) {
        SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
        SKALASSERT(event != NULL);
        switch (event->type) {
        case SKAL_NET_EV_CONN :
            SKALASSERT(-1 == gClientSockid);
            gClientSockid = event->conn.commSockid;
            gHasConnected = true;
            break;
        case SKAL_NET_EV_DISCONN :
            {
                SKALASSERT(gClientSockid == event->sockid);
                SkalNetSocketDestroy(gNet, gClientSockid);
                gClientSockid = -1;
            }
            break;
        case SKAL_NET_EV_IN :
            if (event->sockid == gSockidPipeServer) {
                stop = true;
            } else {
                const char* json = (const char*)(event->in.data);
                bool hasnull = false;
                for (int i = 0; (i < event->in.size_B) && !hasnull; i++) {
                    if ('\0' == json[i]) {
                        hasnull = true;
                    }
                }
                SKALASSERT(hasnull);
                SkalMsg* msg = SkalMsgCreateFromJson(json);
                SKALASSERT(msg != NULL);
                if (strcmp(SkalMsgName(msg), "skal-init-master-born") == 0) {
                    SkalMsg* resp = SkalMsgCreate("skal-init-domain",
                            "skal-master");
                    SkalMsgSetIFlags(resp, SKAL_MSG_IFLAG_INTERNAL);
                    SkalMsgAddString(resp, "domain", "local");
                    char* respjson = SkalMsgToJson(resp);
                    SkalMsgUnref(resp);
                    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
                            event->sockid, respjson, strlen(respjson) + 1);
                    SKALASSERT(SKAL_NET_SEND_OK == result);
                    free(respjson);
                }
                // else: discard all other messages
                SkalMsgUnref(msg);
            }
            break;
        default :
            SKALPANIC_MSG("Pseudo-skald does not expect SkalNet of type %d",
                    (int)event->type);
            break;
        }
        SkalNetEventUnref(event);
    } // loop
}


static RTBool testThreadEnterGroup(void)
{
    gHasConnected = false;
    SkalPlfInit();
    SkalMsgInit();
    SkalPlfThreadMakeSkal_DEBUG("TestThread");
    gNet = SkalNetCreate(NULL);

    // Create pipe to allow pseudo-skald to terminate cleanly
    gSockidPipeServer = SkalNetServerCreate(gNet, "pipe://", 0, NULL, 0);
    SKALASSERT(gSockidPipeServer >= 0);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    SKALASSERT(event != NULL);
    SKALASSERT(SKAL_NET_EV_CONN == event->type);
    SKALASSERT(gSockidPipeServer == event->sockid);
    gSockidPipeClient = event->conn.commSockid;
    SkalNetEventUnref(event);

    unlink(SOCKPATH);
    gServerSockid = SkalNetServerCreate(gNet, "unix://" SOCKPATH, 0, NULL, 0);
    SKALASSERT(gServerSockid >= 0);
    gPseudoSkaldThread = SkalPlfThreadCreate("skald", pseudoSkald, NULL);
    bool connected = SkalThreadInit("unix://" SOCKPATH);
    SKALASSERT(connected);
    return RTTrue;
}

static RTBool testThreadExitGroup(void)
{
    SkalThreadExit();
    char c ='x';
    SkalNetSend_BLOCKING(gNet, gSockidPipeClient, &c, 1);
    SkalPlfThreadJoin(gPseudoSkaldThread);
    gPseudoSkaldThread = NULL;
    SkalNetDestroy(gNet);
    gNet = NULL;
    gServerSockid = -1;
    gClientSockid = -1;
    unlink(SOCKPATH);
    SkalPlfThreadUnmakeSkal_DEBUG();
    SkalMsgExit();
    SkalPlfExit();
    SKALASSERT(gHasConnected);
    return RTTrue;
}


RTT_GROUP_START(TestThreadSimple, 0x00060001u,
        testThreadEnterGroup, testThreadExitGroup)

static bool gPinged = false;
static int gError = -1;
static int gResult = -1;

static bool testSimpleProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgName(msg), "quit") == 0) {
        return false;
    }
    if (cookie != (void*)0xdeadbabe) {
        gError = 1;
    } else if (strcmp(SkalMsgName(msg), "ping") != 0) {
        gError = 2;
    } else if (strcmp(SkalMsgSender(msg), "TestThread@local")!=0) {
        gError = 3;
    } else if (strcmp(SkalMsgRecipient(msg), "simple@local") != 0) {
        gError = 4;
    } else {
        gPinged = true;
        gError = 0;
        gResult = 1;
    }
    usleep(1); // simulate some work
    return true;
}

RTT_TEST_START(skal_simple_should_create_thread)
{
    gPinged = false;
    gError = -1;
    gResult = -1;
    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "simple";
    cfg.processMsg = testSimpleProcessMsg;
    cfg.cookie = (void*)0xdeadbabe;
    SkalThreadCreate(&cfg);
}
RTT_TEST_END

RTT_TEST_START(skal_simple_should_send_ping_msg)
{
    SkalMsg* msg = SkalMsgCreate("ping", "simple");
    RTT_ASSERT(msg != NULL);
    SkalMsgSend(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_simple_should_receive_ping_msg)
{
    // Wait for thread to receive and deal with ping msg
    for (int i = 0; (i < 10000) && !gPinged; i++) {
        usleep(100);
    }
    RTT_EXPECT(gPinged);
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
    if (strcmp(SkalMsgName(msg), "ping") == 0) {
        int64_t count = SkalMsgGetInt(msg, "count");
        if (count != (int64_t)gMsgRecv) {
            gError++;
        }
        gMsgRecv++;
    }
    usleep(100); // simulate work
    return true;
}

static bool testStufferProcessMsg(void* cookie, SkalMsg* msg)
{
    if (strcmp(SkalMsgName(msg), "kick") == 0) {
        SkalMsg* msg2 = SkalMsgCreate("ping", "receiver");
        SkalMsgAddInt(msg2, "count", gMsgSend);
        SkalMsgSend(msg2);
        gMsgSend++;

        if (gMsgSend < 1000) {
            // Send a message to myself to keep going
            SkalMsg* msg3 = SkalMsgCreate("kick", "stuffer");
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
    cfg.name = "receiver";
    cfg.processMsg = testReceiverProcessMsg;
    cfg.queueThreshold = 5;
    SkalThreadCreate(&cfg);

    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "stuffer";
    cfg.processMsg = testStufferProcessMsg;
    SkalThreadCreate(&cfg);
}
RTT_TEST_END

RTT_TEST_START(skal_stress_kick_off)
{
    SkalMsg* msg = SkalMsgCreate("kick", "stuffer");
    SkalMsgSend(msg);
}
RTT_TEST_END

RTT_TEST_START(skal_stress_should_have_sent_and_recv_100_msg)
{
    // Give enough time to send and process 1000 messages (at most 10s)
    for (int i = 0; i < 100*1000; i++) {
        usleep(100);
        if (gMsgRecv >= 1000) {
            break;
        }
    }

    RTT_EXPECT(1000 == gMsgSend);
    RTT_EXPECT(1000 == gMsgRecv);
    RTT_EXPECT(0 == gError);
}
RTT_TEST_END

RTT_GROUP_END(TestThreadStress,
        skal_stress_should_create_threads,
        skal_stress_kick_off,
        skal_stress_should_have_sent_and_recv_100_msg)
