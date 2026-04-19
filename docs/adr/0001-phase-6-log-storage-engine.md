# ADR-0001: Phase 6 - Porting a log storage engine in C++

## Status
Accepted

## Context
This repository is the continuation of the distributed-messaging project (https://github.com/alchevrier/distributed-messaging?tab=readme-ov-file), which was meant to understand how a production distributed messaging system works from the ground and understand the different problems it was originally fixing. Phase 5 was the step in which we tackled key issues which prevented the project from being production ready in a particular scenario: single producer, single consumer on the write path. You may find the reference p50,p99,p99.99 and p100 here (https://github.com/alchevrier/distributed-messaging?tab=readme-ov-file).

In Phase 6, I decided to port the log-storage-engine part that was benchmarked for production readiness in C++ to prove the following: 
- Achieve lower and more deterministic p99 latency than the JVM equivalent by eliminating GC pauses, futex syscalls, and object header overhead.
- We can ensure data structures respect hardware cache line boundaries at compile time through C++ standard library traits and concepts
- Utilising lock-free principles as we will work around the single-producer/single-consumer case which is the one used by the Java project as benchmark reference

The benchmark measures the mmap store path only. msync is excluded — flush policy is caller-determined. This is consistent with the Java Phase 5 methodology, which measured FileChannel.write() into the page cache without fsync.

## Decision

- **SPSC ring buffer**: Single Producer/Single Consumer allows lock-free coordination without a mutex. The producer and consumer each own their index exclusively — no CAS, no contention. On Linux, a mutex contending on futex costs a syscall; SPSC eliminates that entirely on the hot path. 
- **C++ Concepts as compile-time hardware-checker**: Using concepts to enforce hardware constraints at compile time — no runtime overhead, no silent layout bugs.
-> FitsCacheLine — ring buffer slot type, rejects anything > 64 bytes
-> IsNotVirtual — ring buffer slot type, rejects structs with vtables that corrupt the layout (std::is_polymorphic_v<T> (negate it))
-> IsPowerOfTwo — ring buffer capacity template parameter, rejects non-power-of-2 at instantiation
-> IsTriviallyCopyable — slab write path, rejects types that need constructor invocation (std::is_trivially_copyable_v<T>)
- **LogSegment backed by mmap with huge pages**: Each LogSegment maps its backing file into the process address space using mmap with MAP_SHARED | MAP_POPULATE | MAP_HUGETLB. MAP_POPULATE pre-faults all pages at construction time, eliminating page fault latency on the write hot path. MAP_HUGETLB uses 2MB pages, reducing TLB entries required for a 1GB segment from 262,144 (4KB pages) to 512, lowering TLB miss frequency on sequential access. Requires huge pages pre-reserved at OS level (nr_hugepages). Construction is via factory function returning std::expected<LogSegment, std::errc> — MAP_FAILED maps to std::errc::not_enough_memory, no exception crosses the boundary. C++23 required.
- **Google Benchmark**: Industry standard way of benchmarking C++ projects (its equivalent is JMH in Java)

## Alternatives considered
- **Optimising the Java implementation for the SPSC use-case**: While it might provides near production number it is still bringing runtime incertainties due to the JVM runtime optimisation (inlining, memory alignment of the heap, GC pauses). Ultimately a JVM-based implementation will have too many question marks to be trusted on the hot path. 
- **Considering other use-cases such as MPSC**: While this is more similar to a production use-case (such as receiving market data from multiple sources and provide it to downstream application), the original benchmark is against the single-producer/single-consumer therefore it would have been unfair to not optimise around this use-case without at first providing benchmark numbers on the Java-application side. SPSC constrains this design to a single writer thread per partition — multi-producer scenarios require upstream serialisation outside the engine's scope. 

## Consequences

## Positive
- **GC no longer in the equation**: I now don't have to worry about an element that is outside of my coding scope which is the JVM. While the JVM provides amazing productive capabilties of memory allocation it comes with downside (inevitable pauses, inlining/de-inlining penalties). The JVM inserts int[] filler objects into unused TLAB tail regions to maintain heap parsability — a requirement for GC heap walking. These phantom allocations consume heap space and appear in heap dumps, contributing to GC pressure that is entirely absent in a C++ slab-allocated design.
- **Eliminating OS calls on the write and read path**: The Java read path acquired a ReentrantReadWriteLock on every access — a futex syscall under contention. The C++ read path replaces this entirely: the consumer updates atomic<int64_t> lastWrittenOffset with memory_order_release after each batch write to the mmap. A reader loads lastWrittenOffset with memory_order_acquire, verifies requestedOffset < lastWrittenOffset, then reads directly from the mmap region. Zero syscalls, zero locks. The happens-before relationship is established purely through the atomic.

## Negative
- **Lifecycle management is now fully my responsibility**: While RAII exists, before in Java the only components that needed lifecycle management was the Arena I allocated. It is now my responsibiltiy everywhere and needs to be managed through mechanism such as RAII, not allowing copy on classes (=delete on copy constructor), using responsible scoping (unique_ptr, delete in destructor when needed). 
- **Overflow if the producer does not back-off**: SPSCQueue::push() returns bool. When the ring buffer is full, push returns false — the caller decides the overflow policy (drop, alert, spin with _mm_pause()). The storage engine never blocks, never throws, never calls into the OS on the hot path. Backpressure responsibility is explicitly pushed to the caller.