import pytest

from lob.ingest import L2SyncError, replay_l2
from lob.ingest.binance import normalize


# tick_size 0.5, qty_step 1: "100.0" -> 200 ticks, quantities map 1:1.
TICK = 0.5
STEP = 1.0


def snapshot(last_id, bids, asks):
    return {"type": "snapshot", "last_update_id": last_id, "bids": bids, "asks": asks}


def depth(ts, U, u, bids=(), asks=()):
    return {"type": "depth", "ts": ts, "U": U, "u": u,
            "bids": [list(b) for b in bids], "asks": [list(a) for a in asks]}


def trade(price, qty, buyer_maker):
    return {"type": "trade", "ts": 0, "price": price, "qty": qty,
            "buyer_maker": buyer_maker, "trade_id": 1}


def test_normalize_trade_and_depth():
    t = normalize("btcusdt@trade", {"e": "trade", "T": 5, "p": "100.1",
                                    "q": "0.5", "m": True, "t": 9})
    assert t == {"type": "trade", "ts": 5, "price": "100.1", "qty": "0.5",
                 "buyer_maker": True, "trade_id": 9}

    d = normalize("btcusdt@depth@100ms",
                  {"e": "depthUpdate", "E": 7, "U": 1, "u": 3,
                   "b": [["100", "1"]], "a": []})
    assert d["type"] == "depth"
    assert d["U"] == 1 and d["u"] == 3 and d["bids"] == [["100", "1"]]

    assert normalize("btcusdt@kline_1m", {"e": "kline"}) is None


def test_l2_replay_snapshot_and_diffs():
    records = [
        snapshot(100, bids=[["100.0", "5"]], asks=[["101.0", "4"]]),
        depth(1, 95, 100),                                   # stale: dropped
        depth(2, 101, 102, bids=[["100.5", "2"]]),           # new best bid
        depth(3, 103, 104, bids=[["100.0", "0"]],            # remove level
              asks=[["101.0", "6"]]),                        # resize level
    ]
    res = replay_l2(records, tick_size=TICK, qty_step=STEP)

    assert res.stats.dropped_stale == 1
    assert res.stats.depth_events == 2
    assert res.stats.gaps == 0

    r0, r1 = res.records
    assert r0["best_bid"] == pytest.approx(100.5)
    assert r0["best_ask"] == pytest.approx(101.0)
    assert r0["mid"] == pytest.approx(100.75)

    assert r1["best_bid"] == pytest.approx(100.5)   # 100.0 level gone
    assert r1["ask_qty"] == pytest.approx(6.0)      # resized in place
    assert r1["open_orders"] == 2


def test_l2_replay_buffers_diffs_before_snapshot():
    records = [
        depth(1, 95, 100),                                   # pre-snapshot, stale
        depth(2, 101, 102, bids=[["100.5", "2"]]),           # pre-snapshot, valid
        snapshot(100, bids=[["100.0", "5"]], asks=[["101.0", "4"]]),
        depth(3, 103, 104, asks=[["101.5", "1"]]),
    ]
    res = replay_l2(records, tick_size=TICK, qty_step=STEP)
    assert res.stats.dropped_stale == 1
    assert res.stats.depth_events == 2
    assert res.records[-1]["best_bid"] == pytest.approx(100.5)


def test_l2_gap_raises_in_strict_mode():
    records = [
        snapshot(100, bids=[["100.0", "5"]], asks=[["101.0", "4"]]),
        depth(1, 101, 102, bids=[["100.5", "2"]]),
        depth(2, 105, 106, bids=[["99.0", "1"]]),            # gap: U != 103
    ]
    with pytest.raises(L2SyncError, match="gap"):
        replay_l2(records, tick_size=TICK, qty_step=STEP)

    res = replay_l2(records, tick_size=TICK, qty_step=STEP, strict=False)
    assert res.stats.gaps == 1
    assert res.stats.depth_events == 2


def test_l2_crossing_add_is_skipped():
    records = [
        snapshot(100, bids=[["100.0", "5"]], asks=[["101.0", "4"]]),
        # A bid appearing at the ask price would match, not rest: skip it.
        depth(1, 101, 102, bids=[["101.0", "2"]]),
    ]
    res = replay_l2(records, tick_size=TICK, qty_step=STEP)
    assert res.stats.crossed_adds_skipped == 1
    assert res.records[-1]["best_bid"] == pytest.approx(100.0)
    assert res.records[-1]["best_ask"] == pytest.approx(101.0)


def test_l2_cvd_from_aggressor_flag():
    records = [
        snapshot(100, bids=[["100.0", "5"]], asks=[["101.0", "4"]]),
        trade("101.0", "1.5", False),   # aggressive buy
        depth(1, 101, 102, bids=[["100.5", "2"]]),
        trade("100.0", "0.5", True),    # aggressive sell
        depth(2, 103, 104, asks=[["101.5", "1"]]),
    ]
    res = replay_l2(records, tick_size=TICK, qty_step=STEP)
    assert res.stats.trades == 2
    assert res.records[0]["cvd"] == pytest.approx(1.5)
    assert res.records[1]["cvd"] == pytest.approx(1.0)
    assert res.records[1]["last_trade_price"] == pytest.approx(100.0)
