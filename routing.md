
Xon/xoff mechanism
------------------

SKAL philosophy is to make programming as easy as possible for the upper
layer. To that effect, SKAL should transparently handle the case where a
thread sends messages at a faster rate that the recipient is able to
process them.

This is done by throttling the sender thread in the following way:
 - When a message from thread A is pushed into thread B's queue, the
   code also checks whether thread B's queue size is full (a queue is
   said to be full when its size is above a threshold set when it's
   created; pushed messages are still enqueued as long as the system has
   the necessary RAM available).
 - If thread B's queue size is full, we need to throttle any thread
   sending to B until its queue size is not full anymore (which is the
   case when the queue size comes below a certain lower threshold; this
   second threshold is lower in order to have an hysteresis effect).
 - The code sends to thread A an 'xoff' message telling it to stop
   sending any message to thread B.
 - The code sends to thread B an 'ntf-xon' message; this tells thread B
   to send an 'xon' message to thread A when its queue is not full
   anymore; thread B mainains a list of other threads waiting for such
   an 'xon' message.
 - When thread B's queue is not full anymore, it sends 'xon' messages to
   all threads blocked on itself.
 - Thread A keeps track of how many 'xoff' messages it received from any
   other thread; every time an 'xon' message is received, a
   corresponding 'xoff' message is decremented; while the count is
   positive, thread A will not process any user message (but will still
   process SKAL messages, such as 'xon')

Note: SKAL messages never cause an xon/xoff trigger.


All possible scenarios for a message sent by thread A to thread B
-----------------------------------------------------------------

If A and B are in the same process, thread A pushes the message directly
into the message queue of thread B. In addition, thread A checks whether
B's queue is full. If it is full:
 - Thread A sends to itself an 'xoff' message
 - Thread A sends to thread B a 'ntf-xon' message
 - When thread B's queue is not full anymore, it sends an 'xon' message
   to thread A

If A and B are in different processes (say X and Y, possibly on
different computers) the following happens:
 - Thread A sends the message to 'skal-mater' for routing (see note 1
   below)
 - 'skal-master' sends the message to skald for routing
 - skald forwards the message to process Y
 - The 'skal-master' thread of process Y receives the message and pushes
   it into thread B's queue
 - If thread B's queue is full, 'skal-master' will send an 'xoff'
   message to thread A (see note 2 below) and an 'ntf-xon' message to
   thread B
 - When thread B's queue is not full anymore, it sends an 'xon' message
   to thread A (see note 2 below)

TODO from here: I am overdoing skal-net; a process would only
communicate with skald, and that's a single UNIX socket.

Note 1: The message is not sent directly by thread A to skald because if
the socket buffer is full, thread A would block and we would have very
little control over that. Instead, 'skal-master' is tasked to throttle
traffic to skald; so fast-sending thread can be throttled back using the
xon/xoff mechanism.

Note 2: In the above case, the 'xoff' sent from 'skal-master' in process Y to
thread A and the 'xon' message sent by thread B to thread A go through
full routing via skald.


User message failures
---------------------

This section deals with failure of a user message to reach its intended
recipient. Please note a recipient queue being full is not considered a
failure and is dealt with the xon/xoff mechanism explained above.
Failures related to SKAL messages are dealt with in the next section.

Please note the following assumptions are being made by the SKAL
framework:
 - UNIX sockets never lose data
 - There is always enough RAM and CPU to process 'xoff' messages
 - A skald daemon never crashes and always deal with messages in a
   timely manner

A user message may fail to reach its intended recipient in the following
conditions:
 - The recipient does not exist; in this case, the message is dropped;
   if the sender requested to be notified of dropped messages, a
   'skal-drop-no-recipient' message is sent back to the sender.
 - The recipient is in the process of shutting itself down; in this
   case, the message is dropped; if the sender requested to be notified
   of dropped messages, a 'skal-drop-shutdown' message is sent back to
   the sender.
 - There is a network outage; in this case, the message is dropped; if
   the sender requested to be notified of dropped messages, a
   'skal-drop-no-network' message is sent back to the sender.

TODO: sort this
depending on the configuration and if the
   chosen network protocol might lose data (eg: UDP), skald might retry
   sending the message a few times; if that does not work, 


Process crash
-------------

A process crashing is considered exceptional, and thus its handling is
not optimised for speed.

If a process crashes for any reason, the following happens:
 - The skald daemon it registered to detects the crash through the lack
   of heartbeats.
 - skald will broadcast to every node on the network the names of the
   threads of the process that just crashed.
 - Any thread receiving this notification waiting for an 'xon' message
   from one of the thread that just crashed will unblock itself wrt the
   crashed thread.

An alarm will be raised indicating that the process is not sending
heartbeats anymore. The upper layer may choose to restart the crashed
process if it wants to.


SKAL message failures
---------------------

Failure for a SKAL to reach its intended recipient is a major problem
because it breaks signals threads sent to each others.

Generally speaking, if messages can't reach their destinations, it
probably means subsequent messages are very likely not to reach their
destination either. Consequently, this kind of situation is handled
through retries. For example, if a thread is waiting on an 'xon' message
from thread B and has not received it after a certain amount of time, it
will send again a 'ntf-xon' message.


TODO: sort this out
-------------------

If a message is sent to a thread that is currently in the process of
terminating itself, the message will be dropped, regardless of its
flags. In addition, if the `SKAL_MSG_FLAG_NTF_DROP` flag is set, the
sender will be notified that the original message has been dropped
because the destination thread is being terminated by a
"skal-msg-drop-shutdown" message, which will have a string field named
"original-marker", which is the marker of the original message.

