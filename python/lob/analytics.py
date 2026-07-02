"""Market microstructure signals computed from aggregated book depth.

All functions take depth as ``(price, quantity)`` pairs sorted best-first —
exactly the shape returned by :meth:`lob.MatchingEngine.depth`. Prices are
integer ticks; outputs that mix price levels (mid, microprice) are floats.

Signals return ``None`` when a side of the book is empty rather than raising:
during replay the book routinely starts empty or goes one-sided.
"""

from __future__ import annotations

from typing import Optional, Sequence, Tuple

Depth = Sequence[Tuple[int, int]]


def mid_price(bids: Depth, asks: Depth) -> Optional[float]:
    """Midpoint of the best bid and ask."""
    if not bids or not asks:
        return None
    return (bids[0][0] + asks[0][0]) / 2.0


def spread(bids: Depth, asks: Depth) -> Optional[int]:
    """Best ask minus best bid, in ticks."""
    if not bids or not asks:
        return None
    return asks[0][0] - bids[0][0]


def microprice(bids: Depth, asks: Depth) -> Optional[float]:
    """Size-weighted mid: leans toward the thinner side's price.

    ``(ask_px * bid_qty + bid_px * ask_qty) / (bid_qty + ask_qty)`` — a
    common short-horizon fair-value estimate: heavy bids push it up.
    """
    if not bids or not asks:
        return None
    (bid_px, bid_qty), (ask_px, ask_qty) = bids[0], asks[0]
    total = bid_qty + ask_qty
    if total == 0:
        return None
    return (ask_px * bid_qty + bid_px * ask_qty) / total


def order_book_imbalance(bids: Depth, asks: Depth, levels: int = 1) -> Optional[float]:
    """OBI over the top ``levels`` levels: ``(Vb - Va) / (Vb + Va)``.

    Ranges from -1 (all ask-side volume) to +1 (all bid-side volume);
    positive values are commonly read as short-horizon buy pressure.
    """
    if not bids or not asks:
        return None
    vb = sum(qty for _, qty in bids[:levels])
    va = sum(qty for _, qty in asks[:levels])
    total = vb + va
    if total == 0:
        return None
    return (vb - va) / total
