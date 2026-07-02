// Per-operation latency percentiles over the mixed workload.
//
// Usage: lob_latency [num_ops] [csv_path]
//
// Prints a percentile summary to stdout; optionally writes the full
// percentile curve as CSV. Timings include steady_clock overhead
// (~20-40 ns per call), which dominates nothing above the p50 here.

#include <algorithm>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <fstream>
#include <numeric>
#include <string>
#include <vector>

#include "workload.hpp"

using Clock = std::chrono::steady_clock;

int main(int argc, char** argv) {
    const std::size_t n = argc > 1 ? std::stoull(argv[1]) : 1'000'000;
    const std::string csv_path = argc > 2 ? argv[2] : "";

    const auto warmup = lobbench::make_mixed(100'000, 7);
    const auto ops    = lobbench::make_mixed(n, 42);

    lob::MatchingEngine eng;
    for (const auto& op : warmup) lobbench::apply(eng, op);

    std::vector<std::uint64_t> ns(ops.size());
    for (std::size_t i = 0; i < ops.size(); ++i) {
        const auto t0 = Clock::now();
        lobbench::apply(eng, ops[i]);
        const auto t1 = Clock::now();
        ns[i] = static_cast<std::uint64_t>(
            std::chrono::duration_cast<std::chrono::nanoseconds>(t1 - t0).count());
    }

    const std::uint64_t total =
        std::accumulate(ns.begin(), ns.end(), std::uint64_t{0});
    std::sort(ns.begin(), ns.end());
    const auto pct = [&](double p) {
        const auto idx = static_cast<std::size_t>(p * static_cast<double>(ns.size()));
        return ns[std::min(ns.size() - 1, idx)];
    };

    std::printf("ops               %zu\n", ns.size());
    std::printf("throughput        %.2f M ops/s\n",
                static_cast<double>(ns.size()) * 1e3 / static_cast<double>(total));
    std::printf("mean              %.1f ns\n",
                static_cast<double>(total) / static_cast<double>(ns.size()));
    std::printf("p50               %llu ns\n", (unsigned long long)pct(0.50));
    std::printf("p90               %llu ns\n", (unsigned long long)pct(0.90));
    std::printf("p99               %llu ns\n", (unsigned long long)pct(0.99));
    std::printf("p99.9             %llu ns\n", (unsigned long long)pct(0.999));
    std::printf("p99.99            %llu ns\n", (unsigned long long)pct(0.9999));
    std::printf("max               %llu ns\n", (unsigned long long)ns.back());

    if (!csv_path.empty()) {
        std::ofstream csv(csv_path);
        csv << "percentile,latency_ns\n";
        for (int i = 0; i <= 1000; ++i) {
            const double p = static_cast<double>(i) / 1000.0;
            csv << p * 100.0 << ',' << pct(p) << '\n';
        }
        std::printf("wrote %s\n", csv_path.c_str());
    }
    return 0;
}
