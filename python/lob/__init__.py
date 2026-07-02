"""Python SDK for the C++ price-time-priority limit order book engine.

The matching logic lives in C++ (``lob._core``, built with pybind11); this
package is the public interface. Quick start::

    import lob

    eng = lob.MatchingEngine()
    eng.submit(id=1, side=lob.Side.Sell, quantity=10, price=100)
    res = eng.submit(id=2, side=lob.Side.Buy, quantity=4, price=105)
    assert res.status == lob.OrderStatus.Filled
    assert res.trades[0].price == 100   # executions happen at the maker price

Prices are integer ticks and quantities are integers: the engine performs
no floating-point arithmetic.
"""

from lob._core import (
    CancelResult,
    MatchingEngine,
    ModifyResult,
    OrderStatus,
    OrderType,
    RejectReason,
    Side,
    StpPolicy,
    SubmitResult,
    TimeInForce,
    Trade,
)

__version__ = "0.1.0"

__all__ = [
    "CancelResult",
    "MatchingEngine",
    "ModifyResult",
    "OrderStatus",
    "OrderType",
    "RejectReason",
    "Side",
    "StpPolicy",
    "SubmitResult",
    "TimeInForce",
    "Trade",
    "__version__",
]
