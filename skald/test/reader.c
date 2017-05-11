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

#include "skal.h"
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <signal.h>
#include <errno.h>
#include <unistd.h>
#include <time.h>


static enum {
    STARTING,
    RUNNING,
    TERMINATING
} gRunningState = STARTING;

static void handleSignal(int signum)
{
    switch (gRunningState) {
    case STARTING:
        fprintf(stderr,
                "Received signal %d, but SKAL has not initialised yet; forcing termination\n",
                signum);
        exit(2);
        break;

    case RUNNING :
        fprintf(stderr, "Received signal %d, terminating...\n", signum);
        fprintf(stderr, "  (send signal again to force termination)\n");
        gRunningState = TERMINATING;
        SkalCancel();
        break;

    case TERMINATING :
        fprintf(stderr,
                "Received signal %d for a 2nd time, forcing termination\n",
                signum);
        exit(2);
    }
}


static int gDelay_us = 2000;

static bool processMsg(void* cookie, SkalMsg* msg)
{
    bool ok = true;
    int64_t* counter = (int64_t*)cookie;
    if (strcmp(SkalMsgName(msg), "subscribe") == 0) {
        const char* group = SkalMsgGetString(msg, "group");
        SkalMsg* subscribeMsg = SkalMsgCreate("skal-subscribe", "skald");
        SkalMsgAddString(subscribeMsg, "group", group);
        SkalMsgSend(subscribeMsg);

    } else if (strcmp(SkalMsgName(msg), "test-pkt") == 0) {
        int64_t n = SkalMsgGetInt(msg, "number");
        if (n != *counter) {
            fprintf(stderr, "Received packet %lld, expected %lld\n",
                    (long long)n, (long long)(*counter));
            ok = false;
        } else {
            if (SkalMsgHasField(msg, "easter-egg")) {
                // This was the last packet
                fprintf(stderr, "XXX received last packet\n");
                ok = false;
            } else {
                fprintf(stderr, "XXX received packet %d\n", (int)(*counter));
                if (gDelay_us > 0) {
                    usleep(gDelay_us); // Simulate some kind of processing
                }
                (*counter)++;
            }
        }
    }
    return ok;
}


static const char* gOptString = "hu:m:p:";

static void usage(int ret)
{
    printf( "Usage: reader [OPTIONS]\n"
            "Receive messages, simulating some processing for each message.\n"
            "Options:\n"
            "  -h           Print this usage information and exit\n"
            "  -u URL       URL to connect to skald\n"
            "  -m GROUP     Receive messages from this multicast GROUP\n"
            "  -p DELAY_us  Pause for DELAY_us after each message; default=%d; can be 0\n",
            gDelay_us);
    exit(ret);
}


int main(int argc, char** argv)
{
    char* url = NULL;
    char* group = NULL;
    int opt = 0;
    while (opt != -1) {
        opt = getopt(argc, argv, gOptString);
        switch (opt) {
        case 'h':
            usage(0);
            break;
        case 'u' :
            if (strstr(optarg, "://") == NULL) {
                url = SkalSPrintf("unix://%s", optarg);
            } else {
                url = SkalStrdup(optarg);
            }
            break;
        case 'm' :
            group = SkalStrdup(optarg);
            break;
        case 'p' :
            if (sscanf(optarg, "%d", &gDelay_us) != 1) {
                fprintf(stderr, "Invalid DELAY_us: '%s'\n", optarg);
                exit(2);
            }
            break;
        default:
            break;
        }
    }

    struct sigaction sa;
    memset(&sa, 0, sizeof(sa));
    sa.sa_handler = handleSignal;
    int ret = sigaction(SIGINT, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGINT) failed: %s [%d]\n",
                strerror(errno), errno);
        exit(1);
    }
    ret = sigaction(SIGTERM, &sa, NULL);
    if (ret < 0) {
        fprintf(stderr, "ERROR: sigaction(SIGTERM) failed: %s [%d]\n",
                strerror(errno), errno);
        exit(1);
    }

    if (!SkalInit(url, NULL, 0)) {
        fprintf(stderr, "Failed to initialise skald (url=%s)\n", url);
        exit(1);
    }
    free(url);
    gRunningState = RUNNING;

    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    cfg.name = "reader";
    cfg.processMsg = processMsg;
    int64_t* counter = malloc(sizeof(counter));
    *counter = 0;
    cfg.cookie = counter;
    cfg.queueThreshold = 10;
    SkalThreadCreate(&cfg);

    if (group != NULL) {
        SkalMsg* msg = SkalMsgCreate("subscribe", "reader");
        SkalMsgAddString(msg, "group", group);
        free(group);
        SkalMsgSend(msg);
    }

    SkalPause();

    // Cleanup
    free(counter);
    SkalExit();
    return 0;
}
