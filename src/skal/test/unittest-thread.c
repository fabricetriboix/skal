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

#include "skal-thread.h"
#include "rttest.h"
#include <stdlib.h>
#include <string.h>


RTT_GROUP_START(TestThreadInitExit, 0x00050001u, NULL, NULL)

RTT_TEST_START(skal_should_initialise_thread)
{
    // NB: This will create the "skal-master" thread
    SkalThreadInit();
}
RTT_TEST_END

RTT_TEST_START(skal_should_deinitialise_thread)
{
    SkalThreadExit();
}
RTT_TEST_END

RTT_GROUP_END(TestThreadInitExit,
        skal_should_initialise_thread,
        skal_should_deinitialise_thread)
