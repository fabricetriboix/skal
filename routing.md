
Assumptions
-----------

The following assumptions are made in order to simplify things:
 - skald (the SKAL daemon) always perform its tasks according to
   requirements and in a timely manner
 - skald is always on, available and never crashes
 - The links between skald and its processes never drop packets and
   never re-order packets (these are UNIX sockets or equivalent)
 - When a process crashes, the link between it and skald behaves like
   the process has closed the link
 - Any node can directly access any other node in any domain; if you
   need to segregate domains, as far as SKAL is concerned, this is a
   network topology issue that should be addresses and solved at a
   system level


Limits
------

The SKAL framework has been designed to cope with thread crashing,
although it assumes such an event is infrequent and that delays
resulting from such a crash are acceptable. After all, you probably want
to develop an application that doesn't ever crash!

In practice, this means a thread blocked on another thread that just
crashed might have to wait for a few dozens of milli-seconds before it
is notified it can resume its work. In actually real practice, this
probably means the thread that has just been unblocked will not be able
to perform its normal function because its chat buddy is no more.


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
 - The code sends to thread A an `xoff` message telling it to stop
   sending any message to thread B.
 - The code sends to thread B an `ntf-xon` message; this tells thread B
   to send an `xon` message to thread A when its queue is not full
   anymore; thread B maintains a list of other threads waiting for such
   an `xon` message.
 - When thread B's queue is not full anymore, it sends `xon` messages to
   all threads blocked on itself.
 - Thread A keeps track of how many `xoff` messages it received from any
   other thread; as long as an `xoff` message has not been cancelled by
   a corresponding `xon` message, thread A will not process any user
   message (but will still process SKAL messages, such as `xon`)

Note: SKAL messages never cause an xon/xoff trigger.


All possible scenarios for a message sent by thread A to thread B
-----------------------------------------------------------------

If A and B are in the same process, thread A pushes the message directly
into the message queue of thread B. In addition, thread A checks whether
B's queue is full. If it is full:
 - Thread A sends to itself an `xoff` message
 - Thread A sends to thread B a `ntf-xon` message
 - When thread B's queue is not full anymore, it sends an `xon` message
   to thread A

If A and B are in different processes, but in the same domain, the
following happens:
 - Thread A sees that thread B is in a different process and sends the
   message to its skald for routing
 - skald receives the message and sees it is in the same domain
 - skald looks up which process owns thread B
 - if the process that owns thread B is directly connected to skald,
   skald forwards the message to that process
 - if the process that owns thread B is connected to another skald, this
   skald forwards the message to the other skald, which, upon reception,
   forwards it to the process owning thread B
 - The `skal-master` thread of the process owning thread B receives the
   message and pushes it into thread B's queue
 - If thread B's queue is full, `skal-master` sends an `xoff` message
   back to thread A (via skald), and a `ntf-xon` message to thread B
 - When thread B's queue is not full anymore, it sends an `xon` message
   to thread A (via skald)

We can see from the above that every skald in a given domain must know
all the threads in that domain, and which skald manages which thread.

If A and B are in different domains, the following happens:
 - Thread A sees that thread B is in a different process and sends the
   message to its skald for routing
 - skald receives the message and sees it is in a different domain; it
   forwards the message to one of the skald it knows are on that domain
 - What happens thereafter is the same as the previous case, starting
   from the 2nd step (included)

We can see here that skald must know of all peer skald's it is connected
to and their respective domains.


User message failures
---------------------

This section deals with failure of a user message to reach its intended
recipient. Please note a recipient queue being full is not considered a
failure and is dealt with the xon/xoff mechanism explained above.
Failures related to SKAL messages are dealt with in the next section.

Please note the following assumptions are being made by the SKAL
framework:
 - UNIX sockets never lose data
 - There is always enough RAM and CPU to process `xoff` messages
 - A skald daemon never crashes and always deal with messages in a
   timely manner

A user message may fail to reach its intended recipient in the following
conditions:
 - The recipient does not exist or is shutting down; in this case, the
   message is dropped; if the sender requested to be notified of dropped
   messages, a `skal-drop-no-recipient` message is sent back to the
   sender.
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

