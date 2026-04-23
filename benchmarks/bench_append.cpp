#include <benchmark/benchmark.h>
#include <llle/log_segment.hpp>
#include <unistd.h>

// 128MB = 64 * 2MB — IsHugePageAligned, fits 2M x 64-byte appends
static constexpr std::size_t SEG_SIZE = 128ULL * 1024 * 1024;
static constexpr std::size_t PAYLOAD_SIZE = 64; // one cache line

static void BM_AppendSingleCacheLine(benchmark::State& state) {
    auto result = llle::LogSegment<SEG_SIZE>::create("/tmp/bench_segment");
    if (!result.has_value()) {
        state.SkipWithError("LogSegment::create failed");
        return;
    }
    auto& segment = result.value();
    alignas(64) char payload[PAYLOAD_SIZE]{};

    for (auto _ : state) {
        benchmark::DoNotOptimize(segment.append(payload, PAYLOAD_SIZE));
    }

    state.SetBytesProcessed(static_cast<int64_t>(state.iterations()) * PAYLOAD_SIZE);
    unlink("/tmp/bench_segment");
}

// 1M iterations — well within 128MB / 64 = 2M slots
BENCHMARK(BM_AppendSingleCacheLine)->Iterations(1 << 20);
BENCHMARK_MAIN();
