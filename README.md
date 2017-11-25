Skal: Scalable application framework
====================================

Introduction
------------

Skal is dual-licensed under the GPLv3 and a commercial license. If you
require the commercial license, please contact me:
"Fabrice Triboix" <ftriboix-at-incise-dot-com>.

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

Concepts
--------

In essence, a skal application is a collection of workers sending
messages to each other. A worker is essentially a message queue
associated with a message processing function.

Workers are identified with a name that looks like "worker@domain",
with "domain" being the name of group of workers (but all workers
within a process have the same domain name). When sending a message to
a worker, you only need its name; skal will figure out where that
worker is (whether in the same process, same computer or another
computer) and will transparently deliver the message.

Large chunks of data (could be gigabytes) can be attached to messages
in the form of "blobs". Duplication of blob data is avoided as much as
possible, even if the messages they are attached to are duplicated. The
underlying representation of a blob could be almost anything: a shared
memory area, a framebuffer on a video card, a packet on a network
processor, a shared object on a NAS, etc.

Skal supports publishing groups. Send a message to that group, and it
will be duplicated to all group members.

Skal has an in-built error management system called alarms. An alarm
can be raised or lowered and can have different severity levels. These
alarms can be monitored using an automated system, or presented on a
dashboard for visual inspection by a human being.

Skal is numa-aware.

Installation
------------

You will need to have a recent version of cmake installed.

Follow the steps:

    $ git clone --recursive https://github.com/fabricetriboix/skal.git
    $ mkdir build-skal
    $ cd build-skal
    $ cmake ../build-skal
    $ make
    $ make test
    $ make install

Doxygen documentation will be generated. Please read it to learn how
to use skal.

Copyright
---------

This software is owned by myself.

This software is dual-license under the GPLv3 and a commercial license.
The text of the GPLv3 is available in the [LICENSE](LICENSE) file.

A commercial license can be provided if you do not wish to be bound by
the terms of the GPLv3, or for other reasons. Please contact me for more
details.

TODO
----

Still to be implemented:
 - multicasting
 - stress tests
 - networking
 - numa
 - alarm reporting
 - performance reporting

