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

#include "skal-net.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


static RTBool skalNetTestGroupEntry(void)
{
    SkalPlfInit();
    unlink("test.sock");
    return RTTrue;
}

static RTBool skalNetTestGroupExit(void)
{
    SkalPlfExit();
    unlink("test.sock");
    return RTTrue;
}

RTT_GROUP_START(TestNetBasic, 0x00110001u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

static SkalNet* gNet = NULL;

RTT_TEST_START(skal_net_basic_should_parse_ip4_string)
{
    uint32_t ip4;
    RTT_EXPECT(SkalNetStringToIp4("1.2.3.4", &ip4));
    RTT_EXPECT(0x01020304 == ip4);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_print_ip4)
{
    char buffer[32];
    SkalNetIp4ToString(0xDEADBEEF, buffer, sizeof(buffer));
    RTT_EXPECT(strncmp(buffer, "222.173.190.239", sizeof(buffer)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_create_set)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_basic_should_destroy_set)
{
    SkalNetDestroy(gNet);
    gNet = NULL;
}
RTT_TEST_END

RTT_GROUP_END(TestNetBasic,
        skal_net_basic_should_parse_ip4_string,
        skal_net_basic_should_print_ip4,
        skal_net_basic_should_create_set,
        skal_net_basic_should_destroy_set)


RTT_GROUP_START(TestNetPipe, 0x00110002u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

static int gServerSockid = -1;
static int gClientSockid = -1;

RTT_TEST_START(skal_net_pipe_should_create_set)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_pipe_should_create_server)
{
    gServerSockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_PIPE, NULL,
            0, (void*)0xcafedeca, 0);
    RTT_ASSERT(gServerSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_pipe_should_have_created_client)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_CONN == event->type);
    RTT_ASSERT(gServerSockid == event->sockid);
    RTT_ASSERT((void*)0xcafedeca == event->context);
    gClientSockid = event->conn.commSockid;
    RTT_ASSERT(gClientSockid >= 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_pipe_should_send_data)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet, gClientSockid,
            "Hello, ", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
    result = SkalNetSend_BLOCKING(gNet, gClientSockid, "World!", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_pipe_should_receive_data)
{
    char buffer[16];
    int index = 0;
    int count = 14;
    while (count > 0) {
        SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
        RTT_ASSERT(event != NULL);
        RTT_ASSERT(SKAL_NET_EV_IN == event->type);
        RTT_ASSERT(gServerSockid == event->sockid);
        RTT_ASSERT((void*)0xcafedeca == event->context);
        RTT_ASSERT(event->in.data != NULL);
        RTT_ASSERT(event->in.size_B > 0);
        RTT_ASSERT(event->in.size_B <= count);
        memcpy(buffer + index, event->in.data, event->in.size_B);
        index += event->in.size_B;
        count -= event->in.size_B;
        SkalNetEventUnref(event);
    }
    RTT_EXPECT(strncmp(buffer, "Hello, World!", sizeof(buffer)) == 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_pipe_should_destroy_set)
{
    SkalNetDestroy(gNet);
    gNet = NULL;
}
RTT_TEST_END

RTT_GROUP_END(TestNetPipe,
        skal_net_pipe_should_create_set,
        skal_net_pipe_should_create_server,
        skal_net_pipe_should_have_created_client,
        skal_net_pipe_should_send_data,
        skal_net_pipe_should_receive_data,
        skal_net_pipe_should_destroy_set)


RTT_GROUP_START(TestNetUnixStream, 0x00110003u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

static SkalNet* gCommNet = NULL;
static int gCommSockid = -1;

RTT_TEST_START(skal_net_unix_stream_should_create_sets)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);

    gCommNet = SkalNetCreate(0);
    RTT_ASSERT(gCommNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_create_server)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gServerSockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_UNIX_STREAM,
            &addr, 0, gNet, 0);
    RTT_ASSERT(gServerSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_create_client)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gCommSockid = SkalNetCommCreate(gCommNet, SKAL_NET_TYPE_UNIX_STREAM,
            NULL, &addr, 0, (void*)0xdeadbabe, 0);
    RTT_ASSERT(gCommSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_recv_conn_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_CONN == event->type);
    RTT_ASSERT(gServerSockid == event->sockid);
    RTT_ASSERT(gNet == event->context);
    gClientSockid = event->conn.commSockid;
    RTT_ASSERT(gClientSockid >= 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_recv_estab_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_ESTABLISHED == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT((void*)0xdeadbabe == event->context);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_close_server)
{
    RTT_EXPECT(SkalNetSocketDestroy(gNet, gServerSockid));
    gServerSockid = -1;
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_send_ping)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "ping", 5);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_recv_ping)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(5 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("ping", event->in.data, 5) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_send_pong)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gClientSockid, "pong", 5);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_recv_pong)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT(5 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("pong", event->in.data, 5) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_stream_should_destroy_sets)
{
    SkalNetDestroy(gNet);
    SkalNetDestroy(gCommNet);
    gNet = NULL;
    gCommNet = NULL;
    gServerSockid = -1;
    gClientSockid = -1;
    gCommSockid = -1;
}
RTT_TEST_END

RTT_GROUP_END(TestNetUnixStream,
        skal_net_unix_stream_should_create_sets,
        skal_net_unix_stream_should_create_server,
        skal_net_unix_stream_should_create_client,
        skal_net_unix_stream_should_recv_conn_ev,
        skal_net_unix_stream_should_recv_estab_ev,
        skal_net_unix_stream_should_close_server,
        skal_net_unix_stream_should_send_ping,
        skal_net_unix_stream_should_recv_ping,
        skal_net_unix_stream_should_send_pong,
        skal_net_unix_stream_should_recv_pong,
        skal_net_unix_stream_should_destroy_sets)


RTT_GROUP_START(TestNetUnixDgram, 0x00110004u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

RTT_TEST_START(skal_net_unix_dgram_should_create_sets)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);

    gCommNet = SkalNetCreate(-1);
    RTT_ASSERT(gCommNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_create_server)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gServerSockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_UNIX_DGRAM,
            &addr, 0, gNet, 0);
    RTT_ASSERT(gServerSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_create_client)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gCommSockid = SkalNetCommCreate(gCommNet, SKAL_NET_TYPE_UNIX_DGRAM,
            NULL, &addr, 0, (void*)0xdeadbabe, 0);
    RTT_ASSERT(gCommSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_recv_estab_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_ESTABLISHED == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT((void*)0xdeadbabe == event->context);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_send_hello)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "Hello, ", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_send_world)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "World!", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_recv_conn_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_CONN == event->type);
    RTT_ASSERT(gServerSockid == event->sockid);
    RTT_ASSERT(gNet == event->context);
    gClientSockid = event->conn.commSockid;
    RTT_ASSERT(gClientSockid >= 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_close_server)
{
    RTT_EXPECT(SkalNetSocketDestroy(gNet, gServerSockid));
    gServerSockid = -1;
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_recv_hello)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(7 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("Hello, ", event->in.data, 7) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_recv_world)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(7 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("World!", event->in.data, 8) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_send_hi)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gClientSockid, "hi", 3);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_recv_hi)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT(3 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("hi", event->in.data, 3) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_dgram_should_destroy_sets)
{
    SkalNetDestroy(gNet);
    SkalNetDestroy(gCommNet);
    gNet = NULL;
    gCommNet = NULL;
    gServerSockid = -1;
    gClientSockid = -1;
    gCommSockid = -1;
}
RTT_TEST_END

RTT_GROUP_END(TestNetUnixDgram,
        skal_net_unix_dgram_should_create_sets,
        skal_net_unix_dgram_should_create_server,
        skal_net_unix_dgram_should_create_client,
        skal_net_unix_dgram_should_recv_estab_ev,
        skal_net_unix_dgram_should_send_hello,
        skal_net_unix_dgram_should_send_world,
        skal_net_unix_dgram_should_recv_conn_ev,
        skal_net_unix_dgram_should_close_server,
        skal_net_unix_dgram_should_recv_hello,
        skal_net_unix_dgram_should_recv_world,
        skal_net_unix_dgram_should_send_hi,
        skal_net_unix_dgram_should_recv_hi,
        skal_net_unix_dgram_should_destroy_sets)


RTT_GROUP_START(TestNetUnixSeqpkt, 0x00110005u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

RTT_TEST_START(skal_net_unix_seqpkt_should_create_sets)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);

    gCommNet = SkalNetCreate(-1);
    RTT_ASSERT(gCommNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_create_server)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gServerSockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_UNIX_SEQPACKET,
            &addr, 0, (void*)0xdeadbeef, 0);
    RTT_ASSERT(gServerSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_create_client)
{
    SkalNetAddr addr;
    snprintf(addr.unix.path, sizeof(addr.unix.path), "test.sock");
    gCommSockid = SkalNetCommCreate(gCommNet, SKAL_NET_TYPE_UNIX_SEQPACKET,
            NULL, &addr, 0, (void*)0xdeadbabe, 0);
    RTT_ASSERT(gCommSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_recv_conn_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_CONN == event->type);
    RTT_ASSERT(gServerSockid == event->sockid);
    RTT_ASSERT((void*)0xdeadbeef == event->context);
    gClientSockid = event->conn.commSockid;
    RTT_ASSERT(gClientSockid >= 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_recv_estab_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_ESTABLISHED == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT((void*)0xdeadbabe == event->context);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_send_hello)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "Hello, ", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_send_world)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "World!", 7);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_close_server)
{
    RTT_EXPECT(SkalNetSocketDestroy(gNet, gServerSockid));
    gServerSockid = -1;
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_recv_hello)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(7 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("Hello, ", event->in.data, 7) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_recv_world)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(7 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("World!", event->in.data, 7) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_send_hi)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gClientSockid, "hi", 3);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_recv_hi)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT(3 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("hi", event->in.data, 3) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_unix_seqpkt_should_destroy_sets)
{
    SkalNetDestroy(gNet);
    SkalNetDestroy(gCommNet);
    gNet = NULL;
    gCommNet = NULL;
    gServerSockid = -1;
    gClientSockid = -1;
    gCommSockid = -1;
}
RTT_TEST_END

RTT_GROUP_END(TestNetUnixSeqpkt,
        skal_net_unix_seqpkt_should_create_sets,
        skal_net_unix_seqpkt_should_create_server,
        skal_net_unix_seqpkt_should_create_client,
        skal_net_unix_seqpkt_should_recv_conn_ev,
        skal_net_unix_seqpkt_should_recv_estab_ev,
        skal_net_unix_seqpkt_should_send_hello,
        skal_net_unix_seqpkt_should_send_world,
        skal_net_unix_seqpkt_should_close_server,
        skal_net_unix_seqpkt_should_recv_hello,
        skal_net_unix_seqpkt_should_recv_world,
        skal_net_unix_seqpkt_should_send_hi,
        skal_net_unix_seqpkt_should_recv_hi,
        skal_net_unix_seqpkt_should_destroy_sets)


RTT_GROUP_START(TestNetTcp, 0x00110006u,
        skalNetTestGroupEntry, skalNetTestGroupExit)

RTT_TEST_START(skal_net_tcp_should_create_sets)
{
    gNet = SkalNetCreate(0);
    RTT_ASSERT(gNet != NULL);

    gCommNet = SkalNetCreate(0);
    RTT_ASSERT(gCommNet != NULL);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_create_server)
{
    SkalNetAddr addr;
    RTT_ASSERT(SkalNetStringToIp4("127.0.0.1", &addr.ip4.address));
    addr.ip4.port = 5678;
    gServerSockid = SkalNetServerCreate(gNet, SKAL_NET_TYPE_IP4_TCP,
            &addr, 0, NULL, 0);
    RTT_ASSERT(gServerSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_create_client)
{
    SkalNetAddr addr;
    RTT_ASSERT(SkalNetStringToIp4("127.0.0.1", &addr.ip4.address));
    addr.ip4.port = 5678;
    gCommSockid = SkalNetCommCreate(gCommNet, SKAL_NET_TYPE_IP4_TCP,
            NULL, &addr, 0, NULL, 0);
    RTT_ASSERT(gCommSockid >= 0);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_recv_conn_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_CONN == event->type);
    RTT_ASSERT(gServerSockid == event->sockid);
    RTT_ASSERT(NULL == event->context);
    gClientSockid = event->conn.commSockid;
    RTT_ASSERT(gClientSockid >= 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_recv_estab_ev)
{
    usleep(1000);
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_ESTABLISHED == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT(NULL == event->context);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_close_server)
{
    RTT_EXPECT(SkalNetSocketDestroy(gNet, gServerSockid));
    gServerSockid = -1;
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_send_ping)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gCommNet,
            gCommSockid, "ping", 5);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_recv_ping)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gClientSockid == event->sockid);
    RTT_ASSERT(5 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("ping", event->in.data, 5) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_send_pong)
{
    SkalNetSendResult result = SkalNetSend_BLOCKING(gNet,
            gClientSockid, "pong", 5);
    RTT_ASSERT(SKAL_NET_SEND_OK == result);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_recv_pong)
{
    SkalNetEvent* event = SkalNetPoll_BLOCKING(gCommNet);
    RTT_ASSERT(event != NULL);
    RTT_ASSERT(SKAL_NET_EV_IN == event->type);
    RTT_ASSERT(gCommSockid == event->sockid);
    RTT_ASSERT(5 == event->in.size_B);
    RTT_ASSERT(event->in.data != NULL);
    RTT_EXPECT(strncmp("pong", event->in.data, 5) == 0);
    SkalNetEventUnref(event);
}
RTT_TEST_END

RTT_TEST_START(skal_net_tcp_should_destroy_sets)
{
    SkalNetDestroy(gNet);
    SkalNetDestroy(gCommNet);
    gNet = NULL;
    gCommNet = NULL;
    gServerSockid = -1;
    gClientSockid = -1;
    gCommSockid = -1;
}
RTT_TEST_END

RTT_GROUP_END(TestNetTcp,
        skal_net_tcp_should_create_sets,
        skal_net_tcp_should_create_server,
        skal_net_tcp_should_create_client,
        skal_net_tcp_should_recv_conn_ev,
        skal_net_tcp_should_recv_estab_ev,
        skal_net_tcp_should_close_server,
        skal_net_tcp_should_send_ping,
        skal_net_tcp_should_recv_ping,
        skal_net_tcp_should_send_pong,
        skal_net_tcp_should_recv_pong,
        skal_net_tcp_should_destroy_sets)
