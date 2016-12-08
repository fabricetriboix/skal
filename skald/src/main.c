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


#define SKALD_DEFAULT_CFGPATH "/etc/skal/skald.cfg"

#define SKALD_SOCKNAME "skald.sock"


static unsigned int gSigCount = 0;

static void handleSignal(int signum)
{
    switch (gSigCount) {
    case 0 :
        fprintf(stderr, "Received signal %d, terminating...\n", signum);
        gSigCount++;
        SkaldTerminate();
        break;

    case 1 :
        fprintf(stderr, "Received signal %d, but termination is in progress\n",
                signum);
        fprintf(stderr, "Send signal again to force termination\n");
        gSigCount++;
        break;

    default :
        fprintf(stderr, "Received signal %d for a third time, forcing termination now\n",
                signum);
        exit(2);
        break;
    }
}


int main(int argc, char** argv)
{
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

    // TODO: parse config file
    //const char* cfgpath = SKALD_DEFAULT_CFGPATH;

    SkaldParams params;
    memset(&params, 0, sizeof(params));
    params.localAddrPath = "/tmp/skald.sock";
    unlink(params.localAddrPath);

    SkaldRun(&params);
    return 0;
}
