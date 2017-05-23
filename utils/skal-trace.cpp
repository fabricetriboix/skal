/* Copyright (c) 2017  Fabrice Triboix
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

#include "skal-cpp.hpp"
#include "skal-msg.h"
#include <cstdlib>
#include <cstdio>
#include <cstring>
#include <csignal>
#include <unistd.h>
#include <cerrno>
#include <iostream>
#include <string>
#include <memory>


namespace {

enum {
    STARTING,
    RUNNING,
    TERMINATING
} gRunningState = STARTING;

static void handleSignal(int signum)
{
    switch (gRunningState) {
    case STARTING :
        fprintf(stderr,
                "Received signal %d, but SKAL has not initialised yet; forcing termination\n",
                signum);
        exit(2);
        break;

    case RUNNING :
        fprintf(stderr, "Received signal %d, terminating...\n", signum);
        fprintf(stderr, "  (send signal again to force termination)\n");
        gRunningState = TERMINATING;
        skal::Cancel();
        break;

    case TERMINATING :
        fprintf(stderr, "Received signal %d again, forcing termination now\n",
                signum);
        exit(2);
        break;
    }
}


const char* gOptString = "hl:";

void usage(int ret)
{
    printf( "Usage: skal-trace [OPTIONS]\n"
            "Connect to the given skald and dumps all messages received and\n"
            "sent by this skald.\n"
            "Options:\n"
            "  -h      Print this usage information and exit\n"
            "  -l URL  URL to connect to skald\n");
    exit(ret);
}


bool processMsg(skal::Msg& msg)
{
    if (msg.Name() == "trace-kick-off") {
        skal::Msg subscribeMsg("skal-subscribe", "skald");
        subscribeMsg.AddField("group", "skal-trace");
        skal::Send(subscribeMsg);

    } else {
        int64_t us = SkalPlfNow_us();
        char timestamp[64];
        SkalPlfTimestamp(us, timestamp, sizeof(timestamp));

        SkalMsg* brutalcast = (SkalMsg*)&msg;
        char* json = SkalMsgToJson(brutalcast);
        std::cout << "====== " << timestamp << std::endl << json << std::endl;
        free(json);
    }
    return true;
}

} // unnamed namespace


int main(int argc, char** argv)
{
    const char* url = nullptr;
    int opt = 0;
    while (opt != -1) {
        opt = getopt(argc, argv, gOptString);
        switch (opt) {
        case 'h' :
            usage(0);
            break;
        case 'l' :
            url = optarg;
            break;
        default :
            break;
        }
    } // getopt loop

    // Setup signal handlers
    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    int ret = sigaction(SIGINT, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGINT) failed: %s [%d]\n",
                strerror(errno), errno);
        return 1;
    }
    ret = sigaction(SIGTERM, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGTERM) failed: %s [%d]\n",
                strerror(errno), errno);
        return 1;
    }

    // Initialise SKAL
    if (!skal::Init(url)) {
        fprintf(stderr, "Failed to connect to skald (url=%s)\n", url);
        exit(1);
    }
    gRunningState = RUNNING;

    // Start "trace" thread and kick it off
    skal::CreateThread("trace", processMsg);
    {
        skal::Msg msg("trace-kick-off", "trace");
        skal::Send(msg);
    }

    // Wait for natural termination
    skal::Pause();

    // Cleanup
    skal::Exit();
    return 0;
}
