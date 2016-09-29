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

#include "rttest.h"
#include <stdlib.h>
#include <stdio.h>
#include <unistd.h>


static RTBool writeOctet(uint8_t octet)
{
    write(STDOUT_FILENO, &octet, 1);
    return RTTrue;
}


int main(int argc, char** argv)
{
    uint32_t* groups = NULL;
    uint16_t ngroups = 0;

    if (argc > 1) {
        ngroups = (uint16_t)(argc - 1);
        groups = calloc(ngroups, sizeof(uint32_t));
        RTASSERT(groups != NULL);
        for (uint16_t i = 0; i < ngroups; i++) {
            long tmp;
            int n = sscanf(argv[i + 1], "%li", &tmp);
            groups[i] = (uint32_t)tmp;
            RTASSERT(n == 1);
        }
    }
    int32_t ret = RTTestRun(writeOctet, groups, ngroups);
    free(groups);

    if (ret < 0) {
        ret = 1;
    } else {
        ret = 0;
    }
    return ret;
}
