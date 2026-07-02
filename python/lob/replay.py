"""Replay order flow through the matching engine and record signals.

Events are plain dataclasses (:class:`Submit` / :class:`Cancel`), so any
source (historical data, a synthetic generator, live feed capture) just
needs to produce them. :func:`replay` applies each event and records a
snapshot row: best quotes, mid, spread, OBI, and cumulative volume delta.

CVD needs the aggressor side of each trade. The Trade object doesn't carry
it, but the replayer knows it anyway because it submitted the order.
Buy-side aggression adds, sell-side subtracts.
"""

from __future__ import annotations

import random
from dataclasses import dataclass, field
from typing import Iterable, List, Optional, Union

from lob import MatchingEngine, OrderType, Side, TimeInForce
from lob import analytics


@dataclass(frozen=True)
class Submit:
    id: int
    side: Side
    quantity: int
    price: Optional[int] = None
    type: OrderType = OrderType.Limit
    tif: TimeInForce = TimeInForce.GTC
    client: int = 0


@dataclass(frozen=True)
class Cancel:
    target: int


Event = Union[Submit, Cancel]


def synthetic_flow(
    n: int,
    seed: int = 42,
    mid: int = 10_000,
    vol: float = 25.0,
    add_ratio: float = 0.55,
    cancel_ratio: float = 0.35,
) -> List[Event]:
    """Synthetic order flow mirroring the C++ benchmark workload.

    ~``add_ratio`` limit adds, ~``cancel_ratio`` cancels of random live
    orders, and market orders for the remainder. Buys are sampled slightly
    below the mid and sells slightly above, so the streams overlap and a
    fraction of limit orders cross on arrival.
    """
    rng = random.Random(seed)
    events: List[Event] = []
    live: List[int] = []
    next_id = 1

    for _ in range(n):
        r = rng.random()
        if r < add_ratio or not live:
            buy = rng.random() < 0.5
            price = max(1, round(rng.gauss(mid, vol)) + (-5 if buy else 5))
            events.append(
                Submit(
                    id=next_id,
                    side=Side.Buy if buy else Side.Sell,
                    quantity=rng.randint(1, 100),
                    price=price,
                    client=next_id % 64,
                )
            )
            live.append(next_id)
            next_id += 1
        elif r < add_ratio + cancel_ratio:
            k = rng.randrange(len(live))
            events.append(Cancel(target=live[k]))
            live[k] = live[-1]
            live.pop()
        else:
            events.append(
                Submit(
                    id=next_id,
                    side=Side.Buy if rng.random() < 0.5 else Side.Sell,
                    quantity=rng.randint(1, 50),
                    type=OrderType.Market,
                    tif=TimeInForce.IOC,
                    client=next_id % 64,
                )
            )
            next_id += 1
    return events


@dataclass
class ReplayResult:
    records: List[dict] = field(default_factory=list)

    def to_frame(self):
        """Records as a pandas DataFrame (pandas required only here)."""
        import pandas as pd

        return pd.DataFrame.from_records(self.records, index="seq")


def replay(
    events: Iterable[Event],
    engine: Optional[MatchingEngine] = None,
    depth_levels: int = 5,
    snapshot_every: int = 1,
) -> ReplayResult:
    """Apply ``events`` to ``engine`` and record a signal snapshot per event.

    ``snapshot_every=k`` keeps every k-th row (the full stream is always
    applied). Each record carries: seq, event kind, best quotes, mid,
    spread, OBI over ``depth_levels``, trade count/volume for the event,
    and running CVD.
    """
    eng = engine if engine is not None else MatchingEngine()
    result = ReplayResult()
    cvd = 0

    for seq, event in enumerate(events):
        traded_qty = 0
        n_trades = 0
        last_price = None
        if isinstance(event, Submit):
            res = eng.submit(
                id=event.id,
                side=event.side,
                quantity=event.quantity,
                price=event.price,
                type=event.type,
                tif=event.tif,
                client=event.client,
            )
            if res.trades:
                traded_qty = sum(t.quantity for t in res.trades)
                n_trades = len(res.trades)
                last_price = res.trades[-1].price
                cvd += traded_qty if event.side == Side.Buy else -traded_qty
            kind = "submit"
        else:
            eng.cancel(event.target)
            kind = "cancel"

        if seq % snapshot_every != 0:
            continue

        bids, asks = eng.depth(levels=depth_levels)
        result.records.append(
            {
                "seq": seq,
                "kind": kind,
                "best_bid": bids[0][0] if bids else None,
                "best_ask": asks[0][0] if asks else None,
                "mid": analytics.mid_price(bids, asks),
                "spread": analytics.spread(bids, asks),
                "microprice": analytics.microprice(bids, asks),
                "obi": analytics.order_book_imbalance(bids, asks, depth_levels),
                "trades": n_trades,
                "traded_qty": traded_qty,
                "last_trade_price": last_price,
                "cvd": cvd,
                "open_orders": eng.open_orders,
            }
        )
    return result
