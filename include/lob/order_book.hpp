#pragma once

#include <cstddef>
#include <deque>
#include <functional>
#include <map>
#include <unordered_map>
#include <vector>

#include "lob/types.hpp"

namespace lob {

// One price level: FIFO queue of resting orders plus cached aggregates.
// head is the oldest order (first to match), tail the newest.
struct PriceLevel {
    Price       price       = 0;
    Quantity    total_qty   = 0;
    std::size_t order_count = 0;
    Order*      head        = nullptr;
    Order*      tail        = nullptr;

    void push_back(Order* o);
    void remove(Order* o);
};

// Pure price-time-priority storage: sorted price levels per side, an id
// index for O(1) cancel, and a node pool so resting orders never move.
// Matching semantics live in MatchingEngine; this class never trades.
class OrderBook {
public:
    using BidMap = std::map<Price, PriceLevel, std::greater<Price>>;
    using AskMap = std::map<Price, PriceLevel, std::less<Price>>;

    // Rests an order at its price level (caller has already matched it).
    // Returns the pooled node; fails (returns nullptr) on duplicate id.
    Order* insert(const Order& order);

    // Removes a resting order: O(1) id lookup + intrusive unlink.
    bool remove(OrderId id);

    // Unlinks an order the caller already holds a pointer to (e.g. a fully
    // filled maker reached through a level's FIFO head during matching).
    void erase(Order* o);

    // Decreases a resting order's quantity in place; keeps time priority.
    void reduce(Order* o, Quantity delta);

    Order*       find(OrderId id);
    const Order* find(OrderId id) const;

    const PriceLevel* best_bid() const;
    const PriceLevel* best_ask() const;

    BidMap&       bids()       { return bids_; }
    AskMap&       asks()       { return asks_; }
    const BidMap& bids() const { return bids_; }
    const AskMap& asks() const { return asks_; }

    std::size_t open_orders() const { return by_id_.size(); }

private:
    Order* allocate(const Order& proto);
    void   release(Order* o);
    void   unlink(Order* o);

    BidMap bids_;
    AskMap asks_;
    std::unordered_map<OrderId, Order*> by_id_;
    std::deque<Order>   pool_;       // stable addresses across growth
    std::vector<Order*> free_list_;  // recycled nodes
};

}  // namespace lob
