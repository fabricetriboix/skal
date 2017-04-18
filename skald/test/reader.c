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


static bool processMsg(void* cookie, SkalMsg* msg)
{
    bool ok = true;
    int64_t* count = (int64_t*)cookie;
    if (strcmp(SkalMsgName(msg), "test-pkt") == 0) {
        int64_t n = SkalMsgGetInt(msg, "number");
        if (n != *count) {
            fprintf(stderr, "Received packet %lld, expected %lld\n",
                    (long long)n, (long long)(*count));
            ok = false;
        } else {
            if (SkalMsgHasField(msg, "easter-egg")) {
                // This was the last packet
                fprintf(stderr, "XXX received last packet\n");
                ok = false;
            } else {
                fprintf(stderr, "XXX received packet %d\n", (int)(*count));
                usleep(2000); // Simulate some kind of processing
                (*count)++;
            }
        }
    }
    return ok;
}


int main(int argc, char** argv)
{
    if (argc != 2) {
        fprintf(stderr, "Usage: ./reader SKTPATH\n");
        exit(2);
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

    char url[128];
    snprintf(url, sizeof(url), "unix://%s", argv[1]);
    if (!SkalInit(url, NULL, 0)) {
        fprintf(stderr, "Failed to initialise skald (url=%s)\n", url);
        exit(1);
    }
    gRunningState = RUNNING;

    SkalThreadCfg cfg;
    memset(&cfg, 0, sizeof(cfg));
    snprintf(cfg.name, sizeof(cfg.name), "reader");
    cfg.processMsg = processMsg;
    int64_t* count = malloc(sizeof(count));
    *count = 0;
    cfg.cookie = count;
    cfg.queueThreshold = 10;
    SkalThreadCreate(&cfg);

    SkalPause();

    // Cleanup
    free(count);
    SkalExit();
    return 0;
}
