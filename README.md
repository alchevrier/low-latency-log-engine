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

### Latency distribution

10,000 repetitions × 100 iterations. Each repetition reports mean time for its 100 iterations — approximates individual call latency. 64-byte payload, CPU governor set to `performance`.

**Unpinned** (`./build/benchmarks/bench_append --benchmark_time_unit=ns`):

```
median    5.1 ns    steady-state L1D-hot cost
p99      12.6 ns    occasional L1D/L2 pressure
p99.9     7,117 ns  OS scheduling / context switch
p99.99    9,624 ns  rare preemption event
p100      9,776 ns  worst single observation
```

**Pinned to core 1** (`taskset -c 1 ./build/benchmarks/bench_append --benchmark_time_unit=ns`):

```
median    4.3 ns    steady-state L1D-hot cost
p99       9.5 ns    occasional L1D/L2 pressure
p99.9     6,566 ns  kernel interrupt / HT sibling interference
p99.99    7,318 ns  kernel interrupt
p100      7,721 ns  worst single observation
```

Pinning to a single core (`taskset`) reduces the tail ~25% by eliminating thread migration. The remaining ~7 µs tail is kernel interrupt overhead and hyperthreading interference — not the engine. Full elimination requires `isolcpus` + IRQ affinity pinned away from the isolated core, which is standard HFT production config but requires a kernel boot parameter (`isolcpus=1`).

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

The Java implementation ([distributed-messaging](https://github.com/alchevrier/distributed-messaging)) went through two phases. Phase 5 replaced `ByteBuffer.allocate()` + `HashMap<Long, Long>` with pre-allocated off-heap `MemorySegment` slabs — eliminating GC pauses at the cost of a small p50 regression. C++ mmap then eliminates the syscall boundary entirely.

| | Java (baseline) | Java (Phase 5) | C++ (mmap) |
|---|---|---|---|
| p50 / median | 3,900 ns | 4,432 ns (+14%) | **5.1 ns** |
| p99 | 6,500 ns | 6,584 ns (flat) | **12.6 ns** |
| p99.99 | 306,000 ns | 70,519 ns (−77%) | **9,624 ns** † |
| p100 | 40,000,000 ns | 11,993,000 ns (−70%) | **9,776 ns** † |
| Max GC pause | 655 ms (growing) | 2 ms flat | none |

† C++ p99.9+ tail is OS scheduling jitter, not the engine. Eliminated by core pinning (`taskset`, `isolcpus`).

> **Methodology**: Java numbers are JMH `SampleTime` on bare-metal Linux, JFR-confirmed. C++ numbers are Google Benchmark `ComputeStatistics` over 10,000 repetitions of 100 iterations each.

The Phase 5 Java optimisation was the right move within the JVM — GC pauses eliminated, tail dramatically improved. The remaining gap to C++ is the syscall boundary: `FileChannel` crosses into the kernel on every write; `mmap` does not. The p50 difference (~870×) is that cost, nothing else.

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

- **Wire SPSC queue into the pipeline** — decouple caller thread from the mmap write. The trading thread cost drops to a single `store(release)` into the ring buffer; a dedicated writer thread drains into `LogSegment`. Currently `LogManager::append()` is synchronous — the caller pays the full append cost directly.
- **`isolcpus` + IRQ affinity** — kernel boot parameter to fully isolate a core from the OS scheduler, eliminating the remaining ~7 µs tail visible in the pinned benchmark.
