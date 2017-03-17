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

#include "skald.h"
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <unistd.h>
#include <errno.h>


#define SKALD_SOCKNAME "skald.sock"


static enum {
    STARTING,
    RUNNING,
    TERMINATING
} gState = STARTING;

static void handleSignal(int signum)
{
    switch (gState) {
    case STARTING :
        fprintf(stderr,
                "Received signal %d, but skald has not started yet; forcing termination\n",
                signum);
        exit(2);
        break;

    case RUNNING :
        fprintf(stderr, "Received signal %d, terminating...\n", signum);
        fprintf(stderr, "Send signal again to force termination\n");
        gState = TERMINATING;
        break;

    case TERMINATING :
    default :
        fprintf(stderr, "Received signal %d again, forcing termination now\n",
                signum);
        exit(2);
        break;
    }
}


static void usage(void)
{
    printf( "%s",
            "skald [-h] [-d DOMAIN] [-s SOCKPATH]\n"
            "  -h            Print this message and exit\n"
            "  -d DOMAIN     Set the skald domain\n"
            "  -s SOCKPATH   Set the path of the UNIX socket file\n"
            "  -f PIDFILE    Fork and write skald PID in PIDFILE\n"
          );
    exit(0);
}


static void parseArgs(int argc, char** argv, SkaldParams* params)
{
    int opt;
    while ((opt = getopt(argc, argv, "hd:s:f:")) != -1) {
        switch (opt) {
        case 'h' :
            usage();
            break;
        case 'd' :
            params->domain = optarg;
            break;
        case 's' :
            params->localAddrPath = optarg;
            break;
        case 'f' :
            fprintf(stderr, "TODO: Fork not implemented yet\n");
            exit(2);
            break;
        default :
            fprintf(stderr, "Unknown argument -%c\n", (char)opt);
            exit(2);
        }
    }
}


int main(int argc, char** argv)
{
    SkalPlfInit();

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

    SkaldParams params;
    memset(&params, 0, sizeof(params));
    parseArgs(argc, argv, &params);
    unlink(params.localAddrPath);

    SkaldRun(&params);

    gState = RUNNING;
    while (RUNNING == gState) {
        pause();
    }

    SkaldTerminate();
    SkalPlfExit();
    return 0;
}
