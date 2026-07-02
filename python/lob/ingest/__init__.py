"""Market data ingestion: collectors and L2 replay.

:mod:`lob.ingest.binance` records normalized depth + trade streams to JSONL
(requires the ``websockets`` package: ``pip install lob-engine[ingest]``).
:mod:`lob.ingest.l2` replays those captures through the matching engine and
computes microstructure signals.
"""

from lob.ingest.l2 import (
    L2ReplayResult,
    L2Stats,
    L2SyncError,
    read_jsonl,
    replay_l2,
)

__all__ = [
    "L2ReplayResult",
    "L2Stats",
    "L2SyncError",
    "read_jsonl",
    "replay_l2",
]
