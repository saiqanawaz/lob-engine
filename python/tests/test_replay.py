import lob
from lob.replay import Cancel, Submit, replay, synthetic_flow


def test_replay_records_signals_and_cvd():
    events = [
        Submit(id=1, side=lob.Side.Buy, quantity=10, price=99),
        Submit(id=2, side=lob.Side.Sell, quantity=10, price=101),
        Submit(id=3, side=lob.Side.Buy, quantity=4, price=101),   # aggressor buy
        Submit(id=4, side=lob.Side.Sell, quantity=6, price=99),   # aggressor sell
        Cancel(target=1),
    ]
    res = replay(events)
    assert len(res.records) == 5

    r0 = res.records[0]
    assert r0["kind"] == "submit"
    assert r0["best_bid"] == 99 and r0["best_ask"] is None
    assert r0["mid"] is None and r0["cvd"] == 0

    r2 = res.records[2]  # buy 4 lifts the 101 ask
    assert r2["trades"] == 1
    assert r2["traded_qty"] == 4
    assert r2["last_trade_price"] == 101
    assert r2["cvd"] == 4

    r3 = res.records[3]  # sell 6 hits the 99 bid
    assert r3["cvd"] == 4 - 6

    r4 = res.records[4]
    assert r4["kind"] == "cancel"
    assert r4["best_bid"] is None  # order 1 had 4 left; cancelled


def test_replay_snapshot_every():
    events = synthetic_flow(1000, seed=1)
    res = replay(events, snapshot_every=10)
    assert len(res.records) == 100
    assert res.records[-1]["seq"] == 990


def test_synthetic_flow_builds_a_two_sided_market():
    res = replay(synthetic_flow(5000, seed=2))
    last = res.records[-1]
    assert last["open_orders"] > 0
    # A healthy synthetic market ends two-sided with trades along the way.
    assert last["best_bid"] is not None
    assert last["best_ask"] is not None
    assert any(r["trades"] > 0 for r in res.records)
    assert any(r["cvd"] != 0 for r in res.records)


def test_ids_are_unique_across_flow():
    events = synthetic_flow(2000, seed=3)
    ids = [e.id for e in events if isinstance(e, Submit)]
    assert len(ids) == len(set(ids))
