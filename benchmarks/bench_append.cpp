#include <benchmark/benchmark.h>
#include <llle/log_segment.hpp>
#include <unistd.h>
#include <algorithm>
#include <cmath>
#include <vector>

// 128MB = 64 * 2MB — IsHugePageAligned, fits 2M x 64-byte appends
static constexpr std::size_t SEG_SIZE = 128ULL * 1024 * 1024;
static constexpr std::size_t PAYLOAD_SIZE = 64; // one cache line

// RAII guard: unlinks the segment file when the process exits.
// Required because the segment is static (created once across all repetitions).
struct SegmentGuard {
    ~SegmentGuard() { unlink("/tmp/bench_segment"); }
};
static SegmentGuard guard;

// Computes the p-th percentile of v.
// ComputeStatistics passes a sorted-or-unsorted vector of per-repetition mean
// times (ns). We sort a copy and index into it.
static double Percentile(const std::vector<double>& v, double pct) {
    std::vector<double> sorted{v};
    std::sort(sorted.begin(), sorted.end());
    const std::size_t idx = static_cast<std::size_t>(
        std::ceil(pct / 100.0 * static_cast<double>(sorted.size()))) - 1;
    return sorted[std::min(idx, sorted.size() - 1)];
}

static void BM_AppendSingleCacheLine(benchmark::State& state) {
    // Static: created once across all repetitions — avoids open/ftruncate/mmap
    // overhead contaminating each repetition's timing.
    static auto result = llle::LogSegment<SEG_SIZE>::create("/tmp/bench_segment");
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
}

// 10,000 repetitions x 100 iterations = 1,000,000 total appends
// 1M x 64B = 64MB — well within the 128MB segment capacity
// Each repetition reports the mean time for 100 iterations — approximates
// individual call latency. 10,000 data points make p99.99 meaningful (1 point).
BENCHMARK(BM_AppendSingleCacheLine)
    ->Repetitions(10'000)
    ->Iterations(100)
    ->ComputeStatistics("p99",    [](const std::vector<double>& v) { return Percentile(v, 99.0);  })
    ->ComputeStatistics("p99.9",  [](const std::vector<double>& v) { return Percentile(v, 99.9);  })
    ->ComputeStatistics("p99.99", [](const std::vector<double>& v) { return Percentile(v, 99.99); })
    ->ComputeStatistics("p100",   [](const std::vector<double>& v) { return Percentile(v, 100.0); })
    ->ReportAggregatesOnly(true); // suppress 10,000 per-repetition lines

BENCHMARK_MAIN();
