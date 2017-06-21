SKAL: Scalable application framework
====================================


Introduction
------------

SKAL is dual-licensed under the GPLv3 and a commercial license. If you
require the commercial license, please contact me:
"Fabrice Triboix" <ftriboix-at-gmail-dot-com>.

SKAL is an multi-threaded, data-driven and scalable application
framework written in C that has the following objectives:
 - Make writing multi-threaded application a piece of cake and
   fool-proof; this is done by relying entirely on messages and getting
   rid of mutexes, semaphores and shared memory
 - Be efficient at moving data around; SKAL aims for low-latency and/or
   high throughput applications
 - Make writing distributed applications transparent; a thread can be
   moved to another process or another computer transparently
 - Fast, small footprint, portable; minimise RAM, CPU and network usage


Getting started
---------------

You will need to install
[rtsys](https://github.com/fabricetriboix/rtsys) and
[cds](https://github.com/fabricetriboix/cds) first. Make sure they are
installed where the compiler can find them.

    $ vim Makefile                    # Adjust your settings
    $ make                            # Build
    $ make test                       # Run unit & integration tests
    $ make install PREFIX=/your/path  # Install into PREFIX

The Makefile will use ccache if available. You can disable it by adding
`CCACHE=` on the command line, like so:

    $ make CCACHE=

Read the doxygen documentation to learn how to use SKAL.


No warranties
-------------

I wrote these pieces of code on my spare time in the hope that they will
be useful. I make no warranty at all on their suitability for use in
any type of software application.


Copyright
---------

This software is dual-license under the GPLv3 and a commercial license.
The text of the GPLv3 is available in the [LICENSE](LICENSE) file.

A commercial license can be provided if you do not wish to be bound by
the terms of the GPLv3, or for other reasons. Please contact me for more
details.
