#pragma once

#include <cstdint>
#include <vector>

#include "lob/types.hpp"

namespace lob {

struct Trade {
    OrderId  maker_id = 0;  // resting order
    OrderId  taker_id = 0;  // incoming order
    Price    price    = 0;  // executions happen at the maker's price
    Quantity quantity = 0;
};

enum class OrderStatus : std::uint8_t {
    Accepted,         // resting on the book, no fills
    PartiallyFilled,  // some fills, remainder resting on the book
    Filled,           // fully executed
    Cancelled,        // remainder cancelled (IOC residual, market residual, STP)
    Rejected,         // never entered the book
};

enum class RejectReason : std::uint8_t {
    None,
    DuplicateId,
    InvalidQuantity,
    InvalidPrice,
    FokNotFillable,
    UnknownOrder,  // cancel/modify target not found
};

struct SubmitResult {
    OrderId              id     = 0;
    OrderStatus          status = OrderStatus::Rejected;
    RejectReason         reason = RejectReason::None;
    Quantity             filled = 0;
    std::vector<Trade>   trades;
    std::vector<OrderId> stp_cancelled;  // resting orders cancelled by self-trade prevention
};

struct CancelResult {
    bool         ok     = false;
    RejectReason reason = RejectReason::None;
};

struct ModifyResult {
    bool         ok            = false;
    RejectReason reason        = RejectReason::None;
    bool         lost_priority = false;  // true when treated as cancel+replace
    OrderStatus  status        = OrderStatus::Accepted;
    Quantity     filled        = 0;      // fills from re-matching after cancel+replace
    std::vector<Trade>   trades;
    std::vector<OrderId> stp_cancelled;
};

}  // namespace lob
