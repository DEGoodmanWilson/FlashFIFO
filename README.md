FlashFIFO
=========

A sample implementation of a FIFO queue in NOR flash.

The Problem
-----------

The goal here is to provide a FIFO queue, only implemented in NOR flash. The FIFO must be persistent. This presents some interesting engineering challenges:

*The FIFO must not lose too much data if the power is cut.
*The FIFO must be able to return as close to its previous state as possible after power is restored.
*The FIFO must minimize the number of erase cycles on the flash.

The Solution
------------

The implementation presented here manages these desiderata using the following mechanisms.

First, every write is preceded by a set of metadata. One byte stores the size of the write (limiting the possible write sizes—this number was chosen because one byte can always be written atomically), and once the write is complete, a second byte is written that flags the data as valid. If the power is interrupted during the write process, the valid flag will never be written, and future accesses to that file will know to skip the invalid write.

Every data page is flagged with a write counter. This way the write pointer in the file handle can be cleanly recovered—simply look for the page with the smallest write counter, and find your place in that page.

Destructive reads—pulling items from the FIFO flags each previous write as having been consumed. This helps locate the read pointer after recovering from a power loss. Pages are erased as soon as they are completely consumed, and hence no longer needed. Should power be lost during a page erase, the start up routines know how to recognize a corrupted page, and trigger a fresh erase on it.

The Procedure
-------------

This code is meant, in part, as a demonstration of TDD (Test-Driven Development), and in particular that TDD can be used for embedded development. TDD is a method for using unit tests to drive the development process. You think of a feature or corner case you wish to add or deal with. You write a test for that feature or corner case. /Then/ you write the code to satisfy that test. If all goes well, you don't break any other tests. In this way, adding a feature or fixing a corner case guarantees that you don't break any existing functionality. Not a single line of code is written that isn't driven by a unit test. For more on TDD, see [Wikipedia's entry](http://en.wikipedia.org/wiki/Test-driven_development).

The unit tests (and a main() procedure) for this library are located in the Tests subfolder. You will need CppUTest installed for the unit tests to compile. See [The CppUTest website](http://www.cpputest.org/).