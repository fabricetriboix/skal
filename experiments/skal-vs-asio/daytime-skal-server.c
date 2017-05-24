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

/* This is a Mickey Mouse piece of code to compare how it looks vs asio.
 *
 * The asio version is available here:
 *
 * http://www.boost.org/doc/libs/1_64_0/doc/html/boost_asio/tutorial/tutdaytime7/src.html
 *
 * The asio version is 148 lines long and, I think, much less readable. The
 * equivalent functionality implemented with skal-net below is 48 lines long
 * and more readable (but I am obviously biased!).
 */

#include "skal-net.h"
#include <stdlib.h>
#include <stdio.h>
#include <time.h>

int main()
{
    SkalNet* net = SkalNetCreate(free);
    (void)SkalNetCreateServer(net, "tcp://127.0.0.1:9001", 0, NULL, 0);
    (void)SkalNetCreateServer(net, "udp://127.0.0.1:9002", 0, NULL, 0);

    for (;;) {
        SkalNetEvent* event = SkalNetPoll_BLOCKING(net);
        switch (event->type) {
        case SKAL_NET_EV_CONN :
            {
                commSockid = event->conn.commSockId;
                time_t now = time(NULL);
                char* buffer = malloc(128);
                ctime_r(&now, buffer);
                if (!SkalNetWantToSend(net, commSockId, true)) {
                    fprintf(stderr, "Can't set want-to-send flag\n");
                    exit(1);
                }
                if (!SkalNetSetContext(net, commSockId, buffer)) {
                    fprintf(stderr, "Can't set context\n");
                    exit(1);
                }
            }
            break;

        case SKAL_NET_EV_OUT :
            {
                char* msg = (char*)event->context;
                SkalNetSendResult result = SkalNetSend_BLOCKING(net,
                        ev->sockid, msg, strlen(msg) + 1);
                if (result != SKAL_NET_SEND_OK) {
                    fprintf(stderr, "Failed to send\n");
                    exit(1);
                }
                SkalNetSocketDestroy(net, ev->sockid);
            }
            break;
        }
        SkalEventUnref(event);
    } // event loop
    SkalNetDestroy(net);
    return 1;
}
