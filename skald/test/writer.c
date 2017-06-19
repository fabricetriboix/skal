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


static int64_t gCount = 0;
static const char* gRecipient = NULL;
static bool gIsMulticast = false;

static enum {
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
        SkalCancel();
        break;

    case TERMINATING :
        fprintf(stderr,
                "Received signal %d for a 2nd time, forcing termination\n",
                signum);
        exit(2);
    }
}


static bool processMsg(void* cookie, SkalMsg* msg)
{
    bool ok = true;
    int64_t* count = (int64_t*)cookie;
    if (SkalStrcmp(SkalMsgName(msg), "kick") == 0) {
        uint8_t flags = 0;
        if (gIsMulticast) {
            flags = SKAL_MSG_FLAG_MULTICAST;
        }
        SkalMsg* pkt = SkalMsgCreateEx("test-pkt", gRecipient, flags, 0);
        SkalMsgAddInt(pkt, "number", *count);
        (*count)++;
        if (*count >= gCount) {
            // This is the last message; send it and wait for the "done" message
            // from the reader thread.
            SkalMsgAddInt(pkt, "easter-egg", 1);
        } else {
            // Send a message to ourselves to keep going
            SkalMsg* kick = SkalMsgCreate("kick", "writer");
            SkalMsgSend(kick);
        }
        SkalMsgSend(pkt);

    } else if (SkalStrcmp(SkalMsgName(msg), "done") == 0) {
        ok = false;
    }
    return ok;
}


static const char* gOptString = "hc:l:mn:";

static void usage(int ret)
{
    printf( "Usage: writer [OPTIONS] RECIPIENT\n"
            "Send messages as fast as possible to RECIPIENT.\n"
            "Options:\n"
            "     RECIPIENT  To whom to send the messages\n"
            "  -h            Print this usage information and exit\n"
            "  -c COUNT      How many messages to send (default: 10)\n"
            "  -l URL        URL to connect to skald\n"
            "  -m            RECIPIENT is a multicast group instead of a thread\n"
            "  -n NAME       Name to use for writer thread (default=writer)\n");
    exit(ret);
}


int main(int argc, char** argv)
{
    long long count = 10;
    char* url = NULL;
    char* name = "writer";
    int opt = 0;
    while (opt != -1) {
        opt = getopt(argc, argv, gOptString);
        switch (opt) {
        case -1 :
            break;
        case 'h' :
            usage(0);
            break;
        case 'c' :
            if (sscanf(optarg, "%lld", &count) != 1) {
                fprintf(stderr, "Invalid COUNT: '%s'\n", optarg);
                exit(2);
            }
            if (count <= 0) {
                fprintf(stderr, "Invalid COUNT: %lld\n", count);
                exit(2);
            }
            break;
        case 'l' :
            if (strstr(optarg, "://") == NULL) {
                url = SkalSPrintf("unix://%s", optarg);
            } else {
                url = SkalStrdup(optarg);
            }
            break;
        case 'm' :
            gIsMulticast = true;
            break;
        case 'n' :
            name = optarg;
            break;
        default :
            usage(1);
            break;
        }
    }
    if (argc - optind < 1) {
        fprintf(stderr, "RECIPIENT is required\n");
        fprintf(stderr, "Run `writer -h` for help\n");
        exit(2);
    }
    gRecipient = argv[optind];
    gCount = count;

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
    cfg.name = name;
    cfg.processMsg = processMsg;
    int64_t* counter = malloc(sizeof(*counter));
    *counter = 0;
    cfg.cookie = counter;
    SkalThreadCreate(&cfg);

    // Kickstart
    SkalMsg* msg = SkalMsgCreate("kick", name);
    SkalMsgSend(msg);

    SkalPause();

    // Cleanup
    free(counter);
    SkalExit();
    return 0;
}
