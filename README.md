# low-latency-log-engine

C++ port of the log storage engine from [distributed-messaging](https://github.com/alchevrier/distributed-messaging) —
benchmarked in Java (Phase 5), now rebuilt at bare metal with no JVM constraints.

## What this is
A lock-free SPSC log storage engine targeting the same benchmark as the Java implementation.
Write path: mmap stores with `MAP_POPULATE` + `MAP_HUGETLB` (2MB pages, 512 TLB entries for a 1GB segment).
Read path: `atomic<int64_t> lastWrittenOffset` with acquire/release — no futex, no syscall.
Overflow policy: `push()` returns `bool`, caller decides.

## Architecture
All non-trivial decisions are documented as ADRs in [docs/adr](docs/adr).

## Status
ADR written and reviewed. Implementation in progress.

## Toolchain
C++23 — CMake + Conan + Google Benchmark
