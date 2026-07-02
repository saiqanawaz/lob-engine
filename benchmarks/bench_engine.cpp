#include <benchmark/benchmark.h>

#include <memory>

#include "workload.hpp"

using namespace lob;
using namespace lobbench;

// Pure insert throughput: one-sided flow, nothing ever crosses.
static void BM_AddOrder(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto orders = make_add_only(n, 1);
    for (auto _ : state) {
        state.PauseTiming();
        auto eng = std::make_unique<MatchingEngine>();
        state.ResumeTiming();
        for (const auto& o : orders) {
            benchmark::DoNotOptimize(eng->submit(o));
        }
        state.PauseTiming();
        eng.reset();  // book teardown stays untimed
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_AddOrder)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

// O(1) cancel path: pre-filled book, cancel every order.
static void BM_CancelOrder(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto orders = make_add_only(n, 2);
    for (auto _ : state) {
        state.PauseTiming();
        auto eng = std::make_unique<MatchingEngine>();
        for (const auto& o : orders) eng->submit(o);
        state.ResumeTiming();
        for (const auto& o : orders) {
            benchmark::DoNotOptimize(eng->cancel(o.id));
        }
        state.PauseTiming();
        eng.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_CancelOrder)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

// Marketable flow: each market order consumes one resting maker.
static void BM_MarketSweep(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    auto makers = make_add_only(n, 3);
    for (auto& m : makers) {
        m.side     = Side::Sell;
        m.quantity = 10;
    }
    for (auto _ : state) {
        state.PauseTiming();
        auto eng = std::make_unique<MatchingEngine>();
        for (const auto& m : makers) eng->submit(m);
        state.ResumeTiming();
        for (std::size_t i = 0; i < n; ++i) {
            NewOrder taker;
            taker.id       = static_cast<OrderId>(n + i + 1);
            taker.client   = 1;
            taker.side     = Side::Buy;
            taker.type     = OrderType::Market;
            taker.tif      = TimeInForce::IOC;
            taker.quantity = 10;
            benchmark::DoNotOptimize(eng->submit(taker));
        }
        state.PauseTiming();
        eng.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_MarketSweep)->Arg(1 << 10)->Arg(1 << 14)->Arg(1 << 17);

// Realistic mixed flow: 55% adds / 35% cancels / 10% market orders.
static void BM_MixedWorkload(benchmark::State& state) {
    const auto n = static_cast<std::size_t>(state.range(0));
    const auto ops = make_mixed(n, 4);
    for (auto _ : state) {
        state.PauseTiming();
        auto eng = std::make_unique<MatchingEngine>();
        state.ResumeTiming();
        for (const auto& op : ops) {
            apply(*eng, op);
        }
        state.PauseTiming();
        eng.reset();
        state.ResumeTiming();
    }
    state.SetItemsProcessed(state.iterations() * static_cast<std::int64_t>(n));
}
BENCHMARK(BM_MixedWorkload)->Arg(1 << 14)->Arg(1 << 17)->Arg(1 << 20);

BENCHMARK_MAIN();
