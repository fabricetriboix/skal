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
#include "skal-thread.h"
#include "skal-blob.h"
#include <string.h>



/*---------------------------------+
 | Public function implementations |
 +---------------------------------*/


bool SkalInit(const char* skaldUrl,
        const SkalAllocator* allocators, int nallocators)
{
    SkalPlfInit();
    SkalBlobInit(allocators, nallocators);

    const char* sktpath = SKAL_DEFAULT_SKALD_PATH;
    if (skaldUrl != NULL) {
        SKALASSERT(strncmp(skaldUrl, "unix://", 7) == 0);
        sktpath = skaldUrl + 7;
    }

    bool ok = SkalThreadInit(sktpath);
    if (!ok) {
        SkalBlobExit();
        SkalPlfExit();
    }
    return ok;
}


void SkalExit(void)
{
    SkalThreadExit();
    SkalBlobExit();
    SkalPlfExit();
}
