"""Strategy performance statistics (requires numpy).

Conventions: ``returns`` are simple per-period returns; ``equity`` is a
cumulative equity/price curve. Annualization uses ``periods_per_year``
(252 for daily bars; pass the real figure for intraday data).
"""

from __future__ import annotations

import numpy as np


def sharpe(returns, periods_per_year: int = 252, risk_free: float = 0.0) -> float:
    """Annualized Sharpe ratio. NaN for fewer than 2 observations."""
    r = np.asarray(returns, dtype=float) - risk_free / periods_per_year
    if r.size < 2:
        return float("nan")
    sd = r.std(ddof=1)
    if sd == 0:
        return float("inf") if r.mean() > 0 else float("nan")
    return float(r.mean() / sd * np.sqrt(periods_per_year))


def sortino(returns, periods_per_year: int = 252, risk_free: float = 0.0) -> float:
    """Annualized Sortino ratio: penalizes downside deviation only."""
    r = np.asarray(returns, dtype=float) - risk_free / periods_per_year
    if r.size < 2:
        return float("nan")
    downside = np.minimum(r, 0.0)
    dd = np.sqrt(np.mean(downside**2))
    if dd == 0:
        return float("inf") if r.mean() > 0 else float("nan")
    return float(r.mean() / dd * np.sqrt(periods_per_year))


def drawdown_series(equity) -> np.ndarray:
    """Fractional drawdown from the running peak at each point (<= 0)."""
    eq = np.asarray(equity, dtype=float)
    peaks = np.maximum.accumulate(eq)
    return eq / peaks - 1.0


def max_drawdown(equity) -> float:
    """Largest peak-to-trough loss as a positive fraction (0.25 == -25%)."""
    if len(equity) == 0:
        return float("nan")
    return float(-drawdown_series(equity).min())


def summary(returns, periods_per_year: int = 252) -> dict:
    """One-line tearsheet: growth, risk-adjusted ratios, drawdown, hit rate."""
    r = np.asarray(returns, dtype=float)
    equity = np.cumprod(1.0 + r)
    total = float(equity[-1] - 1.0) if r.size else float("nan")
    ann = float(equity[-1] ** (periods_per_year / r.size) - 1.0) if r.size else float("nan")
    return {
        "periods": int(r.size),
        "total_return": total,
        "annualized_return": ann,
        "annualized_vol": float(r.std(ddof=1) * np.sqrt(periods_per_year)) if r.size > 1 else float("nan"),
        "sharpe": sharpe(r, periods_per_year),
        "sortino": sortino(r, periods_per_year),
        "max_drawdown": max_drawdown(equity),
        "hit_rate": float((r > 0).mean()) if r.size else float("nan"),
    }
