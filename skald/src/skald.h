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

#ifndef SKALD_h_
#define SKALD_h_

/** SKALD
 *
 * @defgroup skald SKALD
 * @addtogroup skald
 * @{
 *
 * SKAL daemon to route messages and manage alarms and groups.
 *
 * A skald daemon is part of a domain, which is a cluster of skald daemons with
 * the same domain name.
 *
 * The jobs of the skald daemon are the following:
 *  - Route messages between threads
 *  - Maintain a register of alarms
 *  - Maintain a register of groups
 *
 * A group is akin to IP multicast. A group is a list of destination threads;
 * sending a message to a group will duplicate the message to all members of the
 * group.
 *
 * Threads are classified in the following categories:
 *  - Managed threads: A managed thread is part of a process that is directly
 *    connected to this skald through its local socket
 *  - Domain threads: A domain thread is in the same domain but not directly
 *    connected to this skald; this means such a thread is connected to another
 *    skald in the same domain
 *  - Foreign threads: Threads in a different domain
 *
 * The top-level `routing.md` file details how messages are routed.
 *
 * In order for a thread not be blocked indefinitely on another thread, skald
 * will respond to `skal-ntf-xon` messages where the recipient does not exist
 * anymore. In such a case, skald will send a `skal-xon` message to the blocked
 * thread in order to unblock it.
 *
 * Skald maintains a register of currently activated alarms. It is possible for
 * another thread to register to the 'alarm' group and receive all alarm
 * notifications. It is also possible to query skald at any time to get the
 * alarms currently activated, using a 'skal-query-alarms' message.
 *
 * Finally, skald maintains a register of groups. A group is a set of
 * destination threads. Any message whose recipient is the group, is duplicated
 * for each thread in that group. Nothing prevents an entry in a group to be
 * another group.
 *
 * A message can only pass through skald once. This is to prevent messages going
 * round in circles (eg: when group A references group B which references group
 * A). In practice, a message is uniquely identified through its marker. Also,
 * skald will not keep track of every single message forever, so it will keep
 * track only of the last 10 seconds worth of messages (this duration is
 * configurable).
 *
 * TODO: Check that a msg does not pass more than once through skald
 * TODO: Implement groups
 * TODO: Implement `skal-query-alarms`
 */

#include "skalcommon.h"
#include "skal-net.h"



/*----------------+
 | Macros & Types |
 +----------------*/


/** Parameters required to run skald */
typedef struct {
    /** Domain this skald belongs to
     *
     * Can be NULL, in which case the default domain will be used.
     */
    const char* domain;

    /** Local address to bind and listen to
     *
     * This is where processes on this computer will connect to. This is
     * typically a UNIX socket. Must not be NULL.
     */
    const char* localUrl;
} SkaldParams;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Run skald
 *
 * This function returns when skald is up and running.
 */
void SkaldRun(const SkaldParams* params);


/** Terminate skald
 *
 * This function might block for a very short time while skald is shutting down.
 * When this function returns, skald has been terminated.
 */
void SkaldTerminate(void);



/* @} */
#endif /* SKALD_h_ */
