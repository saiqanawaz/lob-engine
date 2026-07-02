import pytest

from lob import analytics


BIDS = [(100, 30), (99, 20)]
ASKS = [(102, 10), (103, 40)]


def test_mid_and_spread():
    assert analytics.mid_price(BIDS, ASKS) == 101.0
    assert analytics.spread(BIDS, ASKS) == 2


def test_microprice_leans_toward_thin_side():
    # bid_qty 30 vs ask_qty 10: heavy bids push fair value toward the ask.
    mp = analytics.microprice(BIDS, ASKS)
    assert mp == pytest.approx((102 * 30 + 100 * 10) / 40)  # 101.5
    assert mp > analytics.mid_price(BIDS, ASKS)


def test_obi_levels():
    # Top of book: (30 - 10) / 40
    assert analytics.order_book_imbalance(BIDS, ASKS) == pytest.approx(0.5)
    # Two levels: (50 - 50) / 100
    assert analytics.order_book_imbalance(BIDS, ASKS, levels=2) == pytest.approx(0.0)


def test_empty_sides_return_none():
    assert analytics.mid_price([], ASKS) is None
    assert analytics.spread(BIDS, []) is None
    assert analytics.microprice([], []) is None
    assert analytics.order_book_imbalance([], ASKS) is None
