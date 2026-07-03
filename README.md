# lob-engine

[![CI](https://github.com/saiqanawaz/lob-engine/actions/workflows/ci.yml/badge.svg)](https://github.com/saiqanawaz/lob-engine/actions/workflows/ci.yml)

A price-time-priority limit order book and matching engine in C++20, with a
Python SDK built on pybind11.

Live demo: [lob-engine.pages.dev](https://lob-engine.pages.dev). The engine
is compiled to WebAssembly and matches orders in the browser.

## Features

- Executions at the maker's price, limit and market orders, GTC/IOC/FOK,
  and configurable self-trade prevention (cancel-resting or cancel-incoming).
  Amends follow exchange convention: a quantity decrease keeps time priority,
  a price change or size increase is treated as cancel+replace.
- O(1) cancels via an id hash map and intrusive doubly-linked FIFO queues
  per price level. Cancels dominate real order flow, so this is the path
  worth optimizing.
- Prices and quantities are integer ticks. There is no floating point
  inside the engine.
- Google Benchmark suite and a per-operation latency percentile tool.
- 65 tests in CI: 33 C++ (Catch2) covering book mechanics, matching,
  time-in-force, and amend/STP semantics, plus 32 Python tests for the
  bindings, analytics, replay, and ingestion layers.

## Design

Each side of the book is a `std::map` from price to price level (bids
sorted descending, asks ascending), so the best level is `begin()` and
creating a level is O(log n) in the number of levels. A price level holds
its resting orders as an intrusive doubly-linked FIFO: the prev/next
pointers live in the order struct itself, so there is no separate list-node
allocation and unlinking an order is O(1). Order nodes come from a pool
(`std::deque` plus a free list) and never move once allocated, which keeps
raw pointers valid while an order rests. An `unordered_map` from order id
to node gives cancel and modify an O(1) lookup; cancels dominate real
message flow, which is why that path gets the constant-time treatment
rather than insert. Matching walks the opposite side from `begin()`,
level by level, FIFO within each level.

`OrderBook` is pure storage and never trades; all matching semantics
(crossing, time-in-force, self-trade prevention, amend rules) live in
`MatchingEngine`. The map-based level container is the deliberate baseline:
it is simple to reason about and the benchmark suite exists to justify any
replacement (a flat sorted vector or price-indexed array would improve
locality near the touch) with measurements rather than folklore.

## Performance

Mixed workload (55% adds / 35% cancels / 10% market orders), single thread,
GCC 13 `-O2`, WSL2 (Ubuntu 24.04):

| Metric | Value |
| --- | --- |
| Mixed-flow throughput | ~14M ops/s |
| Cancel throughput | up to 42M ops/s |
| Latency p50 / p99 / p99.9 | 80 ns / 374 ns / 1.5 µs |

Tail maximums (~1.4 ms) are OS scheduler jitter on a non-isolated core.
The full percentile curve is in `benchmarks/results/latency_percentiles.csv`.

## Build (C++)

```bash
cmake -S . -B build -G Ninja
cmake --build build
ctest --test-dir build            # 33 Catch2 tests
./build/benchmarks/lob_bench      # Google Benchmark suite
./build/benchmarks/lob_latency    # latency percentiles (+ optional CSV)
```

## Python SDK

```bash
pip install .          # compiles the C++ core via scikit-build-core
```

```python
import lob

eng = lob.MatchingEngine()
eng.submit(id=1, side=lob.Side.Sell, quantity=10, price=100)
res = eng.submit(id=2, side=lob.Side.Buy, quantity=12, price=101)
res.status, res.filled, res.trades
bids, asks = eng.depth()             # aggregated book, best first
```

Run the binding tests with `pip install .[test] && pytest`.

## Data ingestion and L2 replay

Record live Binance depth diffs and trades. Uses the public market-data
endpoints, so no API key is needed; pass `--us` for Binance.US:

```bash
pip install ".[ingest]"
python -m lob.ingest.binance --symbol btcusdt --duration 60 --out btc.jsonl
```

Replay a capture through the engine and compute signals (mid, spread,
microprice, OBI, CVD from the aggressor flag):

```python
from lob.ingest import read_jsonl, replay_l2

res = replay_l2(read_jsonl("btc.jsonl"), tick_size=0.01, qty_step=1e-5)
res.records[-1]          # per-event signal rows
res.stats                # sync quality: stale drops, gaps, crossed adds
```

Binance publishes L2 (aggregate levels), not L3 (individual orders), so the
replayer follows the documented Binance sync algorithm (snapshot bracketing,
update-id contiguity) and maintains one synthetic resting order per price
level. Trades are not matched through the book, since depth diffs are already
post-trade state; they drive CVD instead. A 30s BTCUSDT sample lives in
`data/btcusdt_sample.jsonl`.

## Research

[`notebooks/obi_forward_returns.ipynb`](notebooks/obi_forward_returns.ipynb)
tests whether order-book imbalance predicts short-horizon mid returns in live
BTCUSDT data: information coefficients and OBI-decile conditional returns at
1s/5s/10s horizons, with the signal measured against the half-spread and the
statistical caveats spelled out. Reproduce with
`pip install ".[research,ingest]"`, a fresh capture, and `jupyter`.

## Web demo (WASM)

`web/` is a static site running the same engine compiled to a single-file
WebAssembly module with Emscripten. No framework and no build step. Live
ladder, synthetic flow player, manual order entry, trade tape, depth chart.

```bash
bash web/wasm/build.sh                       # rebuild web/js/lob-engine.js (needs em++)
python -m http.server 8123 --directory web   # then open http://localhost:8123
```

The built module is committed, so the site deploys as-is to any static host.
Production runs on Cloudflare Pages; redeploy with:

```bash
npx wrangler pages deploy web --project-name lob-engine --branch main
```

## Layout

```
include/lob/   engine headers (order_book, matching_engine, types, events)
src/           engine implementation
tests/         Catch2 unit tests
benchmarks/    Google Benchmark suite + latency percentile tool
python/        pybind11 bindings and the `lob` Python package
web/           static site + WASM build of the engine
```
