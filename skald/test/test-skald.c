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
#include "skal-net.h"
#include "skal-msg.h"
#include "skal-thread.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>
#include <unistd.h>


#define SOCKPATH "test-skald.sock"
static SkalPlfThread* gSkaldContainer = NULL;

static void skaldContainer(void* arg)
{
    (void)arg;
    SkaldParams params;
    memset(&params, 0, sizeof(params));
    params.domain = "sandbox";
    params.localAddrPath = SOCKPATH;
    unlink(SOCKPATH);
    SkaldRun(&params);
}


static RTBool testSkaldEnterGroup(void)
{
    SkalPlfInit();
    SkalPlfThreadMakeSkal_DEBUG("TestSkaldThread");
    gSkaldContainer = SkalPlfThreadCreate("skald-container",
            skaldContainer, NULL);
    usleep(10000); // wait for skald to be ready
    bool ok = SkalThreadInit(SOCKPATH);
    SKALASSERT(ok);
    return RTTrue;
}

static RTBool testSkaldExitGroup(void)
{
    SkalThreadExit();
    usleep(10000); // Wait for `skal-master` etc. to die
    SkaldTerminate();
    SkalPlfThreadJoin(gSkaldContainer);
    gSkaldContainer = NULL;
    unlink(SOCKPATH);
    SkalPlfThreadUnmakeSkal_DEBUG();
    SkalPlfExit();
    return RTTrue;
}


RTT_GROUP_START(TestSkaldConnect, 0x00100001u,
        testSkaldEnterGroup, testSkaldExitGroup)

RTT_TEST_START(skald_cnx_should_have_correct_domain)
{
    // `skal-master-thread` should have received the domain name from skald and
    // updated it.
    const char* domain = SkalDomain();
    RTT_ASSERT(domain != NULL);
    RTT_ASSERT(strcmp(domain, "sandbox") == 0);
}
RTT_TEST_END

RTT_GROUP_END(TestSkaldConnect,
        skald_cnx_should_have_correct_domain)
