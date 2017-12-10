Skal: Scalable application framework
====================================

Introduction
------------

Skal is an multi-threaded, data-driven and scalable application
framework written in C++ that has the following objectives:
 - Make writing multi-threaded application a piece of cake and
   fool-proof; this is done by relying entirely on messages and getting
   rid of mutexes, semaphores and (most of) shared memory
 - Be efficient at moving data around; skal aims for low-latency and/or
   high throughput applications
 - Make writing distributed applications transparent; a worker can be
   moved to another process or another computer transparently
 - Fast, small footprint, portable; minimise RAM, CPU and network usage

The current version is a development version which is limited to a
single process (i.e. no networking).

Show me some code!
------------------

Here is a silly example of how you can use skal:

    #include <skal/skal.hpp>
    #include <iostream>

    int main()
    {
        // Create a worker
        skal::worker_t::create("my worker",
                [] (std::unique_ptr<skal::msg_t> msg)
                {
                    std::cout << "Received message " << msg->action();
                    return msg->action() != "stop";
                });

        // Send the worker a message
        skal::send(skal::msg_t::create("my worker", "hi"));

        // Send the worker another message
        skal::send(skal::msg_t::create("my worker", "stop"));

        // Wait for all workers to finish
        skal::wait();
        return 0;
    }

Concepts
--------

In essence, a skal application is a collection of workers sending
messages to each other. A worker is essentially a message queue
associated with a message processing function. A worker runs on its
own thread.

Workers are identified with a name that looks like "worker@domain",
with "domain" being the name of a group of workers (however, all
workers within a process must have the same domain name). When sending
a message to a worker, all you need is its name; skal will figure out
where that worker is (whether in the same process, same computer or
somewhere on the network) and will transparently deliver the message.

If a worker is receiving messages too fast from another worker (maybe
because it is doing some time-consuming processing), skal will
automatically pause the sending worker until the receiving worker can
process messages again. This throttling mechanism is completely
transparent to you (except that the sending worker will be paused and
will not process any messages until the receiving worker is ready).

Large chunks of data (could be gigabytes) can be attached to messages
in the form of "blobs". Duplication of blob data is avoided as much as
possible, even if the messages they are attached to are duplicated. The
underlying representation of a blob could be almost anything: a shared
memory area, a framebuffer on a video card, a packet on a network
processor, a shared object on a NAS, etc.

Skal supports publishing groups. Send a message to that group, and it
will be duplicated to all group members. If a worker wants to
subscribe to a group, it just has to send a "skal-subcribe" to that
group; optionally, it can add a "filter" field to the message to
provide a regex to filter the messages it wants to receive based on
the message action string. To stop receiving message, just send a
"skal-unsubscribe" to the group.

Skal has an in-built error management system called alarms. An alarm
can be raised or lowered and can have different severity levels. These
alarms can be monitored using an automated system, or presented on a
dashboard for visual inspection by a human being.

Skal is numa-aware.

Installation
------------

You can't install skal, sorry.

This is actually a deliberate decision. To use skal, bring it into
your project and link against it there.

To build skal, you will need to have a recent version of cmake
installed.

Follow the steps:

    $ git clone --recursive https://github.com/fabricetriboix/skal.git
    $ mkdir build-skal
    $ cd build-skal
    $ cmake -DCMAKE_BUILD_TYPE=Release../build-skal
    $ make
    $ make test

Doxygen documentation will be generated. Please read it to learn how
to use skal.

Copyright
---------

This software is owned by myself.

This software is dual-license under the GPLv3 and a commercial license.
The text of the GPLv3 is available in the [LICENSE](LICENSE) file.

A commercial license can be provided if you do not wish to be bound by
the terms of the GPLv3, or for other reasons. Please contact me for more
details at: "Fabrice Triboix" ftriboix-at-incise-dot-co.

TODO
----

Still to be implemented:
 - networking
 - numa
 - alarm reporting
 - performance reporting

Internals
=========

Throttling
----------

If a worker A is sending messages to worker B faster than what worker
B can handle, worker A will be throttled. This means that worker A
will be paused until worker B can handle messages again, or after a
certain timeout.

The details are the following:

When worker's B queue goes over its threshold, a "skal-xoff" message
will be sent to worker A (please note that the message is passed to
worker B in any case). When worker A receives the "skal-xoff" message,
it will pause itself (meaning that it will process only internal
messages). When worker B's queue is low enough (currently defined as
half its threshold), it will send a "skal-xon" message to worker A.
When worker A receives this message, it will come out of its paused
state and will resume processing its messages.

It is possible that worker A in the above case is overwhelming more
than one receiving worker. In such a case, worker A may receive many
"skal-xoff" messages from many workers. If that happens, worker A will
be paused until all these workers sent it "skal-xon" messages.

It is possible that worker A never receives a "skal-xon" message
because worker B exits the application (this can happen because it
crashed, or because of a network outage, etc.). To remedy such a
problem, worker A will come out of its paused state unconditionally
after a certain timeout.
