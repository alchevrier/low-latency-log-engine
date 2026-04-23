# low-latency-log-engine

C++ port of the log storage engine from [distributed-messaging](https://github.com/alchevrier/distributed-messaging) — rebuilt at bare metal to isolate the cost of the write I/O model.

## What this is

A lock-free SPSC log storage engine built to HFT production constraints.

- **Write path**: `memcpy` into a `mmap`-backed segment — one relaxed load, one bounds check, one store with `memory_order_release`. Zero syscalls on the hot path.
- **Read path**: `atomic<size_t> lastWrittenOffset` with `memory_order_acquire` — no futex, no syscall.
- **Overflow policy**: `append()` returns `bool`, caller decides.
- **Memory**: `madvise(MADV_HUGEPAGE)` for transparent huge pages. Heap is never touched after startup.

## Architecture

| Component | File | Responsibility |
|-----------|------|----------------|
| Compile-time contracts | `include/llle/concepts.hpp` | `FitsCacheLine`, `IsPowerOfTwo`, `IsHugePageAligned`, `IsTriviallyCopyable`, `IsNotVirtual` |
| Lock-free ring buffer | `include/llle/spsc_queue.hpp` | SPSC queue with acquire/release ordering, cache-line-aligned head/tail/buffer — built, tested, not yet wired into the pipeline (see [Roadmap](#roadmap)) |
| Log segment | `include/llle/log_segment.hpp` | mmap-backed segment, RAII, factory via `std::expected` |
| Log manager | `include/llle/log_manager.hpp` | Directory-scanning manager, precomputed power-of-2 index, zero-syscall hot path |

All non-trivial decisions are documented as ADRs in [docs/adr](docs/adr).

## Results

Measured on 12 × 5600 MHz, L1D 48 KiB, L2 1280 KiB, L3 18432 KiB. CPU governor set to `performance`. Release build (`-O3`).

### Throughput

```
BM_AppendSingleCacheLine   16.1 ns   3.70 GB/s   (1,048,576 iterations, 64-byte payload)
```

### Profiling (`perf stat`)

```
IPC:               0.97   (near 1 instruction retired per cycle)
backend-bound:    68.2%   (memory wall — store bandwidth to L1D)
branch mispredict: 0.33%  (negligible)
```

68% backend-bound means the bottleneck is moving bytes through the memory hierarchy, not the logic. There is no algorithmic waste to remove — this is the irreducible cost of sequential writes.

### Heap allocation (`valgrind --tool=massif`)

Heap is flat at ~85 KB for the entire 1,048,576-iteration loop. The ~90 KB peak occurs at startup during Google Benchmark's framework initialisation (`libstdc++` runtime, `dl_init`) — not in `append`. Zero heap allocation on the hot path.

### Comparison with Java Phase 5

The Java implementation ([distributed-messaging](https://github.com/alchevrier/distributed-messaging), Phase 5) uses `FileChannel` scatter-gather writes (one syscall per append: header slab + payload). Measured with JMH `SampleTime` on the same machine.

| | Java (Phase 5, post-fix) | C++ (mmap) |
|---|---|---|
| p50 | 4,432 ns | 16.1 ns (mean) |
| p99 | 6,584 ns | — |
| p99.99 | 70,519 ns | — |
| Max GC pause | 2 ms | none |

> **Methodology note**: Java numbers are JMH `SampleTime` percentiles (individual call durations sampled and histogrammed). C++ is Google Benchmark mean throughput — percentiles require custom statistics and are not yet instrumented. The p99/p99.99 cells will be filled once percentile sampling is added to the benchmark harness.

The ~275× mean latency difference is the cost of a `FileChannel` syscall versus a `memcpy` into a mapped page. The JVM and GC are secondary — the bottleneck was the I/O model. Both implementations are allocation-free on the hot path in their respective Phase 5 form.

## Test coverage

18 tests, all passing.

```
test_concepts.cpp      — 12 tests  (compile-time concept enforcement)
test_spsc_queue.cpp    —  2 tests  (single-threaded correctness + concurrent producer/consumer)
test_log_segment.cpp   —  3 tests  (factory success, bad path, append-until-full)
test_log_manager.cpp   —  1 test   (directory scan, partition mapping)
```

## Build

```bash
# Install dependencies
conan install . --output-folder=build --build=missing

# Configure and build
source build/conanbuild.sh
cmake -B build -DCMAKE_PREFIX_PATH="$(pwd)/build" -DCMAKE_BUILD_TYPE=Release
cmake --build build

# Run tests
./build/tests/tests

# Run benchmark (disable CPU scaling first)
sudo cpupower frequency-set -g performance
./build/benchmarks/bench_append
```

## Toolchain

C++23 — CMake 3.28, Conan 2.27.1, GCC 13.3.0, Google Benchmark 1.9.1, GTest 1.15.0

## Roadmap

- **Wire SPSC queue into the pipeline** — decouple caller thread from the mmap write. The trading thread cost drops to a single `store(release)` into the ring buffer; a dedicated writer thread drains into `LogSegment`. Currently `LogManager::append()` is synchronous — the caller pays the 16 ns directly.
- **Percentile benchmark statistics** — add p99/p99.99 via `benchmark::RegisterStatistics` to complete the comparison table with the Java Phase 5 numbers.
