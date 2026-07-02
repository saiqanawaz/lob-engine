import math

import pytest

np = pytest.importorskip("numpy")

from lob import tearsheet


def test_max_drawdown_known_curve():
    #        peak ---v         trough
    equity = [100, 120, 90, 130, 110]
    assert tearsheet.max_drawdown(equity) == pytest.approx(0.25)  # 120 -> 90


def test_drawdown_series_nonpositive():
    dd = tearsheet.drawdown_series([100, 120, 90, 130])
    assert (dd <= 0).all()
    assert dd[0] == 0.0


def test_sharpe_known_value():
    r = [0.01, -0.005, 0.02, 0.0, 0.01]
    expected = np.mean(r) / np.std(r, ddof=1) * math.sqrt(252)
    assert tearsheet.sharpe(r) == pytest.approx(expected)


def test_sortino_exceeds_sharpe_for_upside_skew():
    # Large gains, small losses: downside deviation < full deviation.
    r = [0.05, -0.01, 0.06, -0.01, 0.04]
    assert tearsheet.sortino(r) > tearsheet.sharpe(r)


def test_all_positive_returns():
    assert tearsheet.sortino([0.01, 0.02, 0.01]) == float("inf")


def test_summary_fields():
    s = tearsheet.summary([0.01, -0.02, 0.015, 0.005])
    assert s["periods"] == 4
    assert s["hit_rate"] == pytest.approx(0.75)
    assert s["total_return"] == pytest.approx(
        (1.01 * 0.98 * 1.015 * 1.005) - 1.0
    )
    assert s["max_drawdown"] == pytest.approx(0.02)
    for key in ("annualized_return", "annualized_vol", "sharpe", "sortino"):
        assert not math.isnan(s[key])
