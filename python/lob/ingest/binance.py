"""Binance market-data collector: depth diffs + trades to JSONL.

Uses the public market-data endpoints (no API key). binance.com geo-blocks
some regions, including the US; ``--us`` switches to Binance.US.

Capture preserves raw fidelity: prices and quantities stay strings exactly
as Binance sends them; tick conversion happens at replay time. The file
contains one JSON record per line:

    {"type": "meta", ...}                     capture parameters
    {"type": "depth", "ts", "U", "u", "bids", "asks"}
    {"type": "snapshot", "last_update_id", "bids", "asks"}
    {"type": "trade", "ts", "price", "qty", "buyer_maker", "trade_id"}

The REST snapshot is fetched after the first depth diff arrives so that the
update-id ranges bracket it, which is what the Binance book-sync algorithm
(implemented in :mod:`lob.ingest.l2`) requires.

Usage::

    python -m lob.ingest.binance --symbol btcusdt --duration 60 --out btc.jsonl
"""

from __future__ import annotations

import argparse
import asyncio
import json
import time
import urllib.request
from typing import Optional

VISION_WS = "wss://data-stream.binance.vision"
VISION_REST = "https://data-api.binance.vision"
US_WS = "wss://stream.binance.us:9443"
US_REST = "https://api.binance.us"


def normalize(stream: str, payload: dict) -> Optional[dict]:
    """Convert one combined-stream message into a capture record."""
    event = payload.get("e", "")
    if event == "trade" or stream.endswith("@trade"):
        return {
            "type": "trade",
            "ts": payload["T"],
            "price": payload["p"],
            "qty": payload["q"],
            "buyer_maker": payload["m"],
            "trade_id": payload["t"],
        }
    if event == "depthUpdate" or "@depth" in stream:
        return {
            "type": "depth",
            "ts": payload["E"],
            "U": payload["U"],
            "u": payload["u"],
            "bids": payload["b"],
            "asks": payload["a"],
        }
    return None


def fetch_snapshot(rest_base: str, symbol: str, limit: int = 1000) -> dict:
    url = f"{rest_base}/api/v3/depth?symbol={symbol.upper()}&limit={limit}"
    with urllib.request.urlopen(url, timeout=10) as resp:
        data = json.load(resp)
    return {
        "type": "snapshot",
        "last_update_id": data["lastUpdateId"],
        "bids": data["bids"],
        "asks": data["asks"],
    }


async def collect(
    symbol: str,
    duration: float,
    out: str,
    ws_base: str = VISION_WS,
    rest_base: str = VISION_REST,
    depth_interval: str = "100ms",
    trades: bool = True,
) -> dict:
    """Record ``duration`` seconds of market data; returns record counts."""
    import websockets  # optional dependency: lob-engine[ingest]

    streams = [f"{symbol.lower()}@depth@{depth_interval}"]
    if trades:
        streams.append(f"{symbol.lower()}@trade")
    url = f"{ws_base}/stream?streams={'/'.join(streams)}"

    counts = {"depth": 0, "trade": 0}
    async with websockets.connect(url, ping_interval=20) as ws:
        with open(out, "w", encoding="utf-8") as f:
            f.write(
                json.dumps(
                    {
                        "type": "meta",
                        "symbol": symbol.upper(),
                        "streams": streams,
                        "ws_base": ws_base,
                        "captured_at_ms": int(time.time() * 1000),
                    }
                )
                + "\n"
            )
            snapshot_done = False
            deadline = time.monotonic() + duration
            while True:
                remaining = deadline - time.monotonic()
                if remaining <= 0:
                    break
                try:
                    raw = await asyncio.wait_for(ws.recv(), timeout=remaining)
                except asyncio.TimeoutError:
                    break
                msg = json.loads(raw)
                rec = normalize(msg.get("stream", ""), msg.get("data", {}))
                if rec is None:
                    continue
                f.write(json.dumps(rec) + "\n")
                counts[rec["type"]] += 1
                if rec["type"] == "depth" and not snapshot_done:
                    # First diff seen: snapshot now so update ids bracket it.
                    snap = await asyncio.to_thread(fetch_snapshot, rest_base, symbol)
                    f.write(json.dumps(snap) + "\n")
                    snapshot_done = True
    return counts


def main(argv=None) -> None:
    p = argparse.ArgumentParser(description="Record Binance depth+trade streams to JSONL")
    p.add_argument("--symbol", default="btcusdt")
    p.add_argument("--duration", type=float, default=30.0, help="seconds to record")
    p.add_argument("--out", default=None, help="output path (default: <symbol>.jsonl)")
    p.add_argument("--depth-interval", default="100ms", choices=["100ms", "1000ms"])
    p.add_argument("--no-trades", action="store_true")
    p.add_argument("--us", action="store_true", help="use Binance.US endpoints")
    args = p.parse_args(argv)

    ws_base, rest_base = (US_WS, US_REST) if args.us else (VISION_WS, VISION_REST)
    out = args.out or f"{args.symbol.lower()}.jsonl"
    counts = asyncio.run(
        collect(
            symbol=args.symbol,
            duration=args.duration,
            out=out,
            ws_base=ws_base,
            rest_base=rest_base,
            depth_interval=args.depth_interval,
            trades=not args.no_trades,
        )
    )
    print(f"wrote {out}: {counts['depth']} depth events, {counts['trade']} trades")


if __name__ == "__main__":
    main()
