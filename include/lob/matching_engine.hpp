#pragma once

#include "lob/events.hpp"
#include "lob/order_book.hpp"
#include "lob/types.hpp"

namespace lob {

enum class StpPolicy : std::uint8_t {
    None,            // self-trades allowed
    CancelResting,   // cancel the resting order, incoming keeps matching
    CancelIncoming,  // cancel the remainder of the incoming order
};

struct NewOrder {
    OrderId     id       = 0;
    ClientId    client   = 0;
    Side        side     = Side::Buy;
    OrderType   type     = OrderType::Limit;
    TimeInForce tif      = TimeInForce::GTC;
    Price       price    = 0;  // ignored for market orders
    Quantity    quantity = 0;
};

// Price-time-priority matching on top of OrderBook.
//
// Semantics:
//  - Executions happen at the maker's (resting) price.
//  - Market orders never rest: any residual is cancelled.
//  - IOC fills what it can, then cancels the residual.
//  - FOK executes fully or is rejected without touching the book.
//  - Duplicate-id detection covers open orders only.
class MatchingEngine {
public:
    explicit MatchingEngine(StpPolicy stp = StpPolicy::None) : stp_(stp) {}

    SubmitResult submit(const NewOrder& req);
    CancelResult cancel(OrderId id);

    // Exchange-standard amend semantics: a quantity decrease at the same
    // price is done in place and keeps time priority; a price change or
    // quantity increase is a cancel+replace — it loses priority and
    // re-matches like a new order.
    ModifyResult modify(OrderId id, Price new_price, Quantity new_qty);

    const OrderBook& book() const { return book_; }
    StpPolicy stp_policy() const { return stp_; }

private:
    template <class OppositeMap>
    void match(Order& taker, Price limit, OppositeMap& opposite,
               std::vector<Trade>& trades, std::vector<OrderId>& stp_cancelled,
               bool& incoming_cancelled);

    bool fok_fillable(const NewOrder& req) const;

    OrderBook book_;
    StpPolicy stp_;
    SeqNum    next_seq_ = 1;
};

}  // namespace lob
