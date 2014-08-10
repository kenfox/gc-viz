gc-viz
======

Animated visualizations of several garbage collection algorithms.

```
make
open MARK_SWEEP_GC.gif
```

The GIF output requires ImageMagick installed. Edit the Makefile to
choose a different algorithm. If you add more data to the sample,
you'll probably have to increase the GC heap size. This is just a toy
after all!

The interesting thing here is the GC algorithm animations, but in
order to excercise the GC, I had to create a small sample program.
The `reference` directory contains Ruby and Scala implementations
of the sample program. The `dkp.cc` that generates the visualizations
implements similar logic with the exception that it has the world's
worst sort.

Here are some notes from one of my talks on GC. I highly recommend
the book _Garbage Collection: Algorithms for Automatic Dynamic Memory
Management_ by Jones and Lins. I haven't read Jones' newer book.
You can also find excellent overviews by Googling "GC algorithm survey";
Paul Wilson's was very useful to me, but there should be newer surveys
available.

What Is Garbage Collection?
===========================

- automatic
- resource - usually memory, but practical for any storage
- management

- very old technology, general purpose ideas
- misunderstanding causes problems, e.g. "map of weak refs" != cache

- goals of GC: higher level code, uncouple systems, improve performance

- different kinds of memory: unintuitive and getting worse:
  - L1 cache 1ns
  - L2 cache 10ns
  - main memory 100ns

Most programmers are well into the phase of computing where we
depend on the compiler and run-time for memory management just as
we depend on the compiler for code generation. Highly constrained
devices with small memories and/or hard real-time guarantees are
still a problem for GC.

Terminology

- root set: active variables (and machine stuff: stack, cpu registers)
- live set: reachable from root set
- garbage: everything else

Algorithms!
===========

## Free At Exit, aka There's Plenty of RAM

`NO_GC` option in the Makefile.

- simplest possible system
- best concurrency (only allocator)
- reasonable for small programs
- definition of "small" increases as technology improves
- no destructors

## Reference Counting

`REF_COUNT_GC` option in the Makefile.

- 1960
- synchronous - destructors useful
- accidentally ammortized (but long pauses possible)
- simple concurrency
- possible to retrofit

- expensive in cpu
- needs extra word per object to hold counts
- no cycles
- complicated api and/or leaky abstraction
- expensive allocator (fragmentation, locality)
- which allocator doesn't matter (time efficient slab allocator)
- threading problems (mutating ref counts - no read-only data!, destructor runs on random thread)

- iOS, file systems

- extensions:
  - deferred ref count
  - deferred free
  - ref count tables
  - N bit (including 1 bit) ref counts

## Mark Sweep

`MARK_SWEEP_GC` option in the Makefile.

- 1960
- traversal required
- simple
- destructors easy, but delayed
- conservative option (traversal can be approximated)
- possible to retrofit

- asynch
- expensive allocator (fragmentation, locality)
- complicated concurrency (multi-color)

- Lua, Flash, Ruby

- extensions:
- deferred free
- mark tables (examine multiple objects at once)

## Mark Compact

`MARK_COMPACT_GC` option in the Makefile.

- 1964
- precise traversal required
- simple
- minimum total memory usage
- super cheap allocator ("bump" allocator with only a few instructions)
- moving objects difficult to retrofit

- needs 3 passes, but can trade memory for performance
- very complicated concurrency (multi-color, barriers)

- general algorithm that can make other problems easier
- example: fair random row selection

```
           first pass through the database compacts rows so that
             there are no gaps in position, e.g. 1, 2, 3, ...

           select * where position > random() order by position limit 1
```

## Copy

`COPY_GC` option in the Makefile.

- 1962
- precise traversal required
- simplest
- super cheap allocator
- work proportional to live data (garbage doesn't matter!)

- semi-spaces
- no destructors - finalize should not be used
- very complicated concurrency (multi-color, barriers)
- moving objects difficult to retrofit

- common degenerate case: per-transaction pool, delete when done

- most useful gc algorithm whenever you have little live data
- non-memory example: web session storage deletion on Amazon SimpleDB

           create new and old domains
           your server reads from new and faults old into it
           nightly: delete the old domain, create new one, and flip

## Generational, Ephemeral and more

- 1984
- hypothesis: most objects die young
- chain together copy collectors
- oldest generation can use a different gc method

- inter-generational references suck
- non-intuitive performance (garbage is cheap, reuse is expensive)

- foundation for all advanced modern gc
