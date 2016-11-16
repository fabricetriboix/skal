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
 * the same domain name. One skald in the cluster must be designated as the
 * gateway, which means that it is in charge with communications with other
 * domains. Domain names must be unique for a given network.
 *
 * The jobs of the skald daemon are the following:
 *  - Route messages between threads
 *  - Monitor which thread is blocked on what thread
 *  - Unblock blocked thread when the blocking thread crashed
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
 * The routing of a message between threads is accomplished in different ways
 * depending on the sender and recipient of the message. If the recipient of the
 * message is a group, the message is duplicated for each thread in the group
 * and the sames rules apply as if the duplicated message is sent to the
 * destination thread. Regardless of the sender thread, the following apply:
 *  - If the recipient thread is a managed thread, skald will forward it to the
 *    corresponding process through the local connection
 *  - If the recipient thread is a domain thread, skald will forward it to the
 *    skald that manages this thread (this means that this skald keeps track of
 *    all managed and domain threads in its domain)
 *  - If the recipient thread is a foreign thread, skald will forward it to the
 *    domain gateway (for each domain, one skald is designated as the gateway
 *    and is in charge of all communications with other domains). The gateway
 *    skald does not keep track of every single foreign thread. Instead it keeps
 *    track of other domain gateways. In practice any message whose recipient is
 *    neither a managed thread not a domain thread is considered a foreign
 *    thread, even if it actually does not exist. The skald gateway of the
 *    corresponding domain will know that the recipient does not exist.
 *
 * It is very important that a thread does not get block indefinitely on another
 * thread that died for some reason. There are 2 cases where a thread might be
 * blocked forever in such a way:
 *  - When the blocking thread terminates naturally without having sent 'xon'
 *    messages to unblock other threads blocked on it
 *  - Or when the blocking thread crashes (in which case it brings down the
 *    whole process with it); from skald's point of view, this is just detected
 *    by the local connection to the process being cut
 * In both cases, skald will come to the rescue and send 'xon' messages to
 * threads blocked on the thread(s) that just died. This means skald will keep
 * track of foreign threads blocked on a given managed thread, on top of domain
 * threads (skald should never see any xon/xoff messages for managed threads,
 * are these are routed and managed by the skal-master thread for that process).
 *
 * Skald also maintains a register of currently activated alarms. It is possible
 * for another thread to register to the 'alarm' group and receive all alarm
 * notifications. It is also possible to query skald at any time to get the
 * alarms currently activated, using a 'skal-query-alarms' message.
 *
 * Finally, skald maintains a register of groups. A group is a set of
 * destination threads. Any message whose recipient is the group, is duplicated
 * for each thread in that group. Nothing prevents an entry in a group to be a
 * group itself.
 *
 * A message can only pass through skald once. This is to prevent messages going
 * round in circles (eg: when group A references group B which references group
 * A). In practice, a message is uniquely identified through its marker. Also,
 * skald will not keep track of every single message forever, so it will keep
 * track only of the last 10 seconds worth of messages (this duration is
 * configurable).
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

    /** Is this skald the designated gateway for the above domain?
     *
     * If you have multiple domains, you must designated one and only one skald
     * to be the gateway for that domain. All communications with other domains
     * will go through that gateway.
     */
    bool isGateway;

    /** Local address to bind and listen to
     *
     * This is where processes on this computer will connect to. This is
     * typically a UNIX socket.
     */
    const SkalNetAddr localAddr;

    /** Other skald daemons to connect to
     *
     * These can be in this domain or different domains.
     *
     * This is an array of `npeers` items. May be NULL if you want this skald to
     * be alone.
     */
    const SkalNetAddr* peers;

    /** Number of items in the `peers` array - must be >= 0 */
    int npeers;

    /** Timeout when polling for network events, in us
     *
     * Set this to <= 0 to use the default value.
     */
    int pollTimeout_us;
} SkaldParams;



/*------------------------------+
 | Public function declarations |
 +------------------------------*/


/** Run skald
 *
 * This function does not return, except when `SkaldTerminate` is called.
 */
void SkaldRun(const SkaldParams* params);


/** Terminate skald
 *
 * This function can typically be called from a signal handler.
 */
void SkaldTerminate(void);



/* @} */
#endif /* SKALD_h_ */
