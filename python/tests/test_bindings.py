"""Binding-layer tests: the C++ engine has its own exhaustive Catch2 suite,
so these verify the Python surface — argument handling, enum round-trips,
result objects — not matching semantics in depth."""

import pytest

import lob


@pytest.fixture
def eng():
    return lob.MatchingEngine()


def test_resting_order_and_best_quotes(eng):
    res = eng.submit(id=1, side=lob.Side.Buy, quantity=10, price=100)
    assert res.status == lob.OrderStatus.Accepted
    assert res.filled == 0
    assert eng.best_bid == (100, 10)
    assert eng.best_ask is None
    assert eng.open_orders == 1


def test_crossing_order_trades_at_maker_price(eng):
    eng.submit(id=1, side=lob.Side.Sell, quantity=10, price=100)
    res = eng.submit(id=2, side=lob.Side.Buy, quantity=10, price=105)

    assert res.status == lob.OrderStatus.Filled
    assert res.filled == 10
    [trade] = res.trades
    assert trade.maker_id == 1
    assert trade.taker_id == 2
    assert trade.price == 100
    assert trade.quantity == 10
    assert eng.open_orders == 0


def test_depth_aggregates_levels_best_first(eng):
    eng.submit(id=1, side=lob.Side.Buy, quantity=10, price=99)
    eng.submit(id=2, side=lob.Side.Buy, quantity=5, price=100)
    eng.submit(id=3, side=lob.Side.Buy, quantity=7, price=100)
    eng.submit(id=4, side=lob.Side.Sell, quantity=3, price=101)

    bids, asks = eng.depth()
    assert bids == [(100, 12), (99, 10)]
    assert asks == [(101, 3)]

    bids_top1, _ = eng.depth(levels=1)
    assert bids_top1 == [(100, 12)]


def test_market_order_requires_no_price(eng):
    eng.submit(id=1, side=lob.Side.Sell, quantity=10, price=100)
    res = eng.submit(
        id=2, side=lob.Side.Buy, quantity=4, type=lob.OrderType.Market
    )
    assert res.status == lob.OrderStatus.Filled
    assert res.trades[0].price == 100


def test_limit_order_without_price_raises(eng):
    with pytest.raises(ValueError, match="requires a price"):
        eng.submit(id=1, side=lob.Side.Buy, quantity=10)


def test_ioc_and_fok(eng):
    eng.submit(id=1, side=lob.Side.Sell, quantity=5, price=100)

    ioc = eng.submit(
        id=2, side=lob.Side.Buy, quantity=8, price=100, tif=lob.TimeInForce.IOC
    )
    assert ioc.status == lob.OrderStatus.Cancelled
    assert ioc.filled == 5

    fok = eng.submit(
        id=3, side=lob.Side.Buy, quantity=8, price=100, tif=lob.TimeInForce.FOK
    )
    assert fok.status == lob.OrderStatus.Rejected
    assert fok.reason == lob.RejectReason.FokNotFillable


def test_reject_reasons(eng):
    bad_qty = eng.submit(id=1, side=lob.Side.Buy, quantity=0, price=100)
    assert bad_qty.reason == lob.RejectReason.InvalidQuantity

    eng.submit(id=2, side=lob.Side.Buy, quantity=10, price=100)
    dup = eng.submit(id=2, side=lob.Side.Buy, quantity=10, price=99)
    assert dup.reason == lob.RejectReason.DuplicateId


def test_cancel(eng):
    eng.submit(id=1, side=lob.Side.Buy, quantity=10, price=100)
    assert eng.cancel(1).ok
    assert eng.best_bid is None

    missing = eng.cancel(1)
    assert not missing.ok
    assert missing.reason == lob.RejectReason.UnknownOrder


def test_modify_semantics(eng):
    eng.submit(id=1, side=lob.Side.Sell, quantity=10, price=100)

    down = eng.modify(1, price=100, quantity=4)
    assert down.ok and not down.lost_priority

    up = eng.modify(1, price=100, quantity=8)
    assert up.ok and up.lost_priority


def test_modify_can_rematch(eng):
    eng.submit(id=1, side=lob.Side.Buy, quantity=10, price=100)
    eng.submit(id=2, side=lob.Side.Sell, quantity=6, price=101, client=2)

    mod = eng.modify(1, price=101, quantity=10)
    assert mod.ok and mod.lost_priority
    assert mod.filled == 6
    assert mod.trades[0].maker_id == 2


def test_self_trade_prevention():
    eng = lob.MatchingEngine(stp=lob.StpPolicy.CancelResting)
    eng.submit(id=1, side=lob.Side.Sell, quantity=5, price=100, client=7)
    eng.submit(id=2, side=lob.Side.Sell, quantity=5, price=100, client=9)

    res = eng.submit(id=3, side=lob.Side.Buy, quantity=8, price=100, client=7)
    assert res.stp_cancelled == [1]
    assert res.trades[0].maker_id == 2
    assert res.filled == 5


def test_version():
    assert lob.__version__
