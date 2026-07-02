# lob-engine

A price-time-priority limit order book and matching engine in C++20, with a
Python SDK built on pybind11.

## Highlights

- **Matching semantics that mirror real exchanges**: executions at the maker's
  price, limit/market orders, GTC/IOC/FOK, configurable self-trade prevention
  (cancel-resting / cancel-incoming), and amend rules where a quantity decrease
  keeps time priority but a price change or size increase loses it.
- **O(1) cancels**: id hash map plus intrusive doubly-linked FIFO queues per
  price level. Cancels dominate real order flow, so this is the hot path.
- **Integer ticks throughout** — no floating point inside the engine.
- **Measured, not claimed**: Google Benchmark suite and a per-operation latency
  percentile tool included.

## Baseline performance

Mixed workload (55% adds / 35% cancels / 10% market orders), single thread,
GCC 13 `-O2`, WSL2 (Ubuntu 24.04):

| Metric | Value |
| --- | --- |
| Mixed-flow throughput | ~14M ops/s |
| Cancel throughput | up to 42M ops/s |
| Latency p50 / p99 / p99.9 | 80 ns / 374 ns / 1.5 µs |

Tail maximums (~1.4 ms) reflect OS scheduler jitter on a non-isolated core;
the full percentile curve is in `benchmarks/results/latency_percentiles.csv`.

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
res.status, res.filled, res.trades   # Filled? how much? against whom?
bids, asks = eng.depth()             # aggregated book, best first
```

Run the binding tests with `pip install .[test] && pytest`.

## Data ingestion and L2 replay

Record live Binance depth diffs + trades (public market-data endpoints, no
API key; use `--us` for Binance.US):

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
level. Trades are not matched through the book — depth diffs are already
post-trade state — they drive CVD instead. A 30s BTCUSDT sample lives in
`data/btcusdt_sample.jsonl`.

## Layout

```
include/lob/   engine headers (order_book, matching_engine, types, events)
src/           engine implementation
tests/         Catch2 unit tests
benchmarks/    Google Benchmark suite + latency percentile tool
python/        pybind11 bindings and the `lob` Python package
```
