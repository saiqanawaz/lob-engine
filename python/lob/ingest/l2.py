"""Replay captured L2 data through the matching engine.

Binance publishes aggregate price-level diffs (L2), not individual orders
(L3), so the book is reconstructed with one synthetic resting order per
price level: new level -> submit, quantity change -> modify, zero -> cancel.

Sync follows the documented Binance algorithm: diffs are buffered until the
REST snapshot record appears, events entirely below ``last_update_id`` are
dropped, the first applied event must bracket ``last_update_id + 1``, and
every later event must be contiguous with the previous one (a gap raises
:class:`L2SyncError` unless ``strict=False``).

Trades are not matched through the engine: depth diffs are already
post-trade state, so matching them would double-count. The trade stream
instead drives CVD via the ``buyer_maker`` aggressor flag.
"""

from __future__ import annotations

import json
from dataclasses import dataclass, field
from typing import Dict, Iterable, Iterator, List, Optional

from lob import MatchingEngine, Side
from lob import analytics
from lob.replay import ReplayResult


class L2SyncError(RuntimeError):
    """Update-id gap: the capture cannot reproduce the true book."""


@dataclass
class L2Stats:
    depth_events: int = 0
    dropped_stale: int = 0
    gaps: int = 0
    trades: int = 0
    crossed_adds_skipped: int = 0


@dataclass
class L2ReplayResult(ReplayResult):
    stats: L2Stats = field(default_factory=L2Stats)


def read_jsonl(path) -> Iterator[dict]:
    with open(path, "r", encoding="utf-8") as f:
        for line in f:
            line = line.strip()
            if line:
                yield json.loads(line)


class _BookBuilder:
    """One synthetic resting order per price level."""

    def __init__(self, tick_size: float, qty_step: float):
        self.eng = MatchingEngine()
        self.tick_size = tick_size
        self.qty_step = qty_step
        self.levels: Dict[Side, Dict[int, int]] = {Side.Buy: {}, Side.Sell: {}}
        self.next_id = 1
        self.crossed_adds_skipped = 0

    def price_ticks(self, price: str) -> int:
        return round(float(price) / self.tick_size)

    def qty_ticks(self, qty: str) -> int:
        return round(float(qty) / self.qty_step)

    def set_level(self, side: Side, price: str, qty: str) -> None:
        pt = self.price_ticks(price)
        qt = self.qty_ticks(qty)
        book = self.levels[side]
        oid = book.get(pt)

        if qt == 0:
            if oid is not None:
                self.eng.cancel(oid)
                del book[pt]
            return
        if oid is not None:
            self.eng.modify(oid, price=pt, quantity=qt)
            return

        # New level. A bid at/above the best ask (or vice versa) would match
        # instead of resting. This is a transient artifact of applying one
        # side of a diff before the other has caught up; skip and count it.
        if side == Side.Buy:
            opposite = self.eng.best_ask
            if opposite is not None and pt >= opposite[0]:
                self.crossed_adds_skipped += 1
                return
        else:
            opposite = self.eng.best_bid
            if opposite is not None and pt <= opposite[0]:
                self.crossed_adds_skipped += 1
                return
        self.eng.submit(id=self.next_id, side=side, quantity=qt, price=pt)
        book[pt] = self.next_id
        self.next_id += 1

    def apply_diff(self, bids: List[List[str]], asks: List[List[str]]) -> None:
        # Removals first, both sides: within one event, deleting emptied
        # levels before adding new ones avoids most transient crossings.
        for price, qty in bids:
            if self.qty_ticks(qty) == 0:
                self.set_level(Side.Buy, price, qty)
        for price, qty in asks:
            if self.qty_ticks(qty) == 0:
                self.set_level(Side.Sell, price, qty)
        for price, qty in bids:
            if self.qty_ticks(qty) != 0:
                self.set_level(Side.Buy, price, qty)
        for price, qty in asks:
            if self.qty_ticks(qty) != 0:
                self.set_level(Side.Sell, price, qty)


def replay_l2(
    records: Iterable[dict],
    tick_size: float,
    qty_step: float,
    depth_levels: int = 5,
    snapshot_every: int = 1,
    strict: bool = True,
) -> L2ReplayResult:
    """Rebuild the book from a capture and record one signal row per diff.

    ``tick_size`` / ``qty_step`` map exchange decimals onto the engine's
    integer ticks (BTCUSDT: 0.01 and 1e-5). Output rows are converted back
    to real units.
    """
    result = L2ReplayResult()
    stats = result.stats
    builder: Optional[_BookBuilder] = None
    pending: List[dict] = []
    last_update_id: Optional[int] = None
    last_u: Optional[int] = None
    synced = False
    cvd = 0.0
    last_trade_price: Optional[float] = None
    seq = 0

    def apply_depth(rec: dict) -> None:
        nonlocal last_u, synced, seq
        assert builder is not None and last_update_id is not None
        if not synced:
            if rec["u"] <= last_update_id:
                stats.dropped_stale += 1
                return
            if rec["U"] > last_update_id + 1:
                stats.gaps += 1
                if strict:
                    raise L2SyncError(
                        f"first event U={rec['U']} does not bracket "
                        f"snapshot last_update_id={last_update_id}"
                    )
            synced = True
        elif last_u is not None and rec["U"] != last_u + 1:
            stats.gaps += 1
            if strict:
                raise L2SyncError(
                    f"update-id gap: expected U={last_u + 1}, got U={rec['U']}"
                )
        last_u = rec["u"]
        builder.apply_diff(rec["bids"], rec["asks"])
        stats.depth_events += 1

        if stats.depth_events % snapshot_every != 0:
            return
        bids, asks = builder.eng.depth(levels=depth_levels)
        mid = analytics.mid_price(bids, asks)
        spread = analytics.spread(bids, asks)
        micro = analytics.microprice(bids, asks)
        result.records.append(
            {
                "seq": seq,
                "ts": rec["ts"],
                "best_bid": bids[0][0] * tick_size if bids else None,
                "best_ask": asks[0][0] * tick_size if asks else None,
                "bid_qty": bids[0][1] * qty_step if bids else None,
                "ask_qty": asks[0][1] * qty_step if asks else None,
                "mid": mid * tick_size if mid is not None else None,
                "spread": spread * tick_size if spread is not None else None,
                "microprice": micro * tick_size if micro is not None else None,
                "obi": analytics.order_book_imbalance(bids, asks, depth_levels),
                "cvd": cvd,
                "last_trade_price": last_trade_price,
                "open_orders": builder.eng.open_orders,
            }
        )
        seq += 1

    for rec in records:
        kind = rec.get("type")
        if kind == "snapshot":
            builder = _BookBuilder(tick_size, qty_step)
            last_update_id = rec["last_update_id"]
            for price, qty in rec["bids"]:
                builder.set_level(Side.Buy, price, qty)
            for price, qty in rec["asks"]:
                builder.set_level(Side.Sell, price, qty)
            for buffered in pending:
                apply_depth(buffered)
            pending.clear()
        elif kind == "depth":
            if builder is None:
                pending.append(rec)  # diff arrived before the snapshot
            else:
                apply_depth(rec)
        elif kind == "trade":
            qty = float(rec["qty"])
            # buyer_maker: the buyer rested, so the aggressor sold.
            cvd += -qty if rec["buyer_maker"] else qty
            last_trade_price = float(rec["price"])
            stats.trades += 1

    stats.crossed_adds_skipped = builder.crossed_adds_skipped if builder else 0
    return result
