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
    if (strcmp(SkalMsgName(msg), "kick") == 0) {
        SkalMsg* pkt = SkalMsgCreate("test-pkt", gRecipient);
        SkalMsgAddInt(pkt, "number", *count);
        (*count)++;
        if (*count >= gCount) {
            // This is the last message
            SkalMsgAddInt(pkt, "easter-egg", 1);
            ok = false;
        } else {
            // Send a message to ourselves to keep going
            SkalMsg* kick = SkalMsgCreate("kick", "writer");
            SkalMsgSend(kick);
        }
        SkalMsgSend(pkt);
    }
    return ok;
}


int main(int argc, char** argv)
{
    if (argc != 4) {
        fprintf(stderr, "Usage: ./writer SKTPATH DST COUNT\n");
        fprintf(stderr, "  SKTPATH   Path to skald UNIX socket\n");
        fprintf(stderr, "  DST       Recipient thread\n");
        fprintf(stderr, "  COUNT     How many messages to send\n");
        exit(2);
    }

    long long tmp;
    if (sscanf(argv[3], "%lld", &tmp) != 1) {
        fprintf(stderr, "ERROR: Invalid COUNT: '%s'\n", argv[3]);
        exit(2);
    }
    if (tmp <= 0) {
        fprintf(stderr, "ERROR: Invalid COUNT: %lld\n", tmp);
        exit(2);
    }
    gCount = tmp;

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

    gRecipient = argv[2];
    char url[128];
    snprintf(url, sizeof(url), "unix://%s", argv[1]);
    if (!SkalInit(url, NULL, 0)) {
        fprintf(stderr, "Failed to initialise skald\n");
        exit(1);
    }
    gRunningState = RUNNING;

    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "writer");
    cfg.processMsg = processMsg;
    int64_t* count = malloc(sizeof(count));
    *count = 0;
    cfg.cookie = count;
    SkalThreadCreate(&cfg);

    // Kickstart
    SkalMsg* msg = SkalMsgCreate("kick", "writer");
    SkalMsgSend(msg);

    SkalPause();

    // Cleanup
    free(count);
    SkalExit();
    return 0;
}
