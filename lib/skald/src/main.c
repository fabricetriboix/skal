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


#define SKALD_DEFAULT_CFGPATH "/etc/skal/skald.cfg"

#define SKALD_SOCKNAME "skald.sock"


static bool gTerminationInProgress = false;

static void handleSignal(int signum)
{
    if (gTerminationInProgress) {
        fprintf(stderr,
                "Received signal %d, but termination is in progress; signal ignored\n");
    } else {
        fprintf(stderr, "Received signal %d, terminating...\n");
        gTerminationInProgress = true;
        SkaldTerminate();
    }
}


int main(int argc, char** argv)
{
    // TODO: parse config file
    //const char* cfgpath = SKALD_DEFAULT_CFGPATH;

    SkaldParams params;
    memset(&params, 0, sizeof(params));
    char path[SKAL_NAME_MAX];
    params.localAddrPath = "/tmp/skald.sock";
    unlink(params.localAddrPath);

    SkaldRun(&params);
    return 0;
}
