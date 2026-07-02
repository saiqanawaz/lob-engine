#include "lob/matching_engine.hpp"

#include <algorithm>
#include <limits>

namespace lob {

namespace {

bool crosses(Side taker_side, Price limit, Price level_price) {
    return taker_side == Side::Buy ? level_price <= limit : level_price >= limit;
}

// Market orders match at any price: model them as a limit at the extreme.
Price effective_limit(const NewOrder& req) {
    if (req.type == OrderType::Limit) return req.price;
    return req.side == Side::Buy ? std::numeric_limits<Price>::max()
                                 : std::numeric_limits<Price>::min();
}

}  // namespace

SubmitResult MatchingEngine::submit(const NewOrder& req) {
    SubmitResult out;
    out.id = req.id;

    if (req.quantity <= 0) {
        out.reason = RejectReason::InvalidQuantity;
        return out;
    }
    if (req.type == OrderType::Limit && req.price <= 0) {
        out.reason = RejectReason::InvalidPrice;
        return out;
    }
    if (book_.find(req.id) != nullptr) {
        out.reason = RejectReason::DuplicateId;
        return out;
    }
    if (req.tif == TimeInForce::FOK && !fok_fillable(req)) {
        out.reason = RejectReason::FokNotFillable;
        return out;
    }

    Order taker;
    taker.id       = req.id;
    taker.client   = req.client;
    taker.side     = req.side;
    taker.tif      = req.tif;
    taker.price    = req.price;
    taker.quantity = req.quantity;
    taker.seq      = next_seq_++;

    const Price limit = effective_limit(req);
    bool incoming_cancelled = false;
    if (req.side == Side::Buy) {
        match(taker, limit, book_.asks(), out.trades, out.stp_cancelled, incoming_cancelled);
    } else {
        match(taker, limit, book_.bids(), out.trades, out.stp_cancelled, incoming_cancelled);
    }
    out.filled = req.quantity - taker.quantity;

    if (taker.quantity == 0) {
        out.status = OrderStatus::Filled;
        return out;
    }

    const bool can_rest = req.type == OrderType::Limit &&
                          req.tif == TimeInForce::GTC && !incoming_cancelled;
    if (can_rest) {
        book_.insert(taker);
        out.status = out.filled > 0 ? OrderStatus::PartiallyFilled : OrderStatus::Accepted;
    } else {
        out.status = OrderStatus::Cancelled;
    }
    return out;
}

template <class OppositeMap>
void MatchingEngine::match(Order& taker, Price limit, OppositeMap& opposite,
                           std::vector<Trade>& trades,
                           std::vector<OrderId>& stp_cancelled,
                           bool& incoming_cancelled) {
    while (taker.quantity > 0) {
        auto it = opposite.begin();
        if (it == opposite.end()) return;
        if (!crosses(taker.side, limit, it->first)) return;

        // Walk the level's FIFO queue. erase/remove may delete the level
        // once it empties, but in that case the captured `next` is already
        // nullptr, so the inner loop exits before touching freed state.
        Order* maker = it->second.head;
        while (maker != nullptr && taker.quantity > 0) {
            Order* next = maker->next;
            if (stp_ != StpPolicy::None && maker->client == taker.client) {
                if (stp_ == StpPolicy::CancelResting) {
                    stp_cancelled.push_back(maker->id);
                    book_.erase(maker);
                } else {  // CancelIncoming
                    incoming_cancelled = true;
                    return;
                }
            } else {
                const Quantity qty = std::min(taker.quantity, maker->quantity);
                trades.push_back({maker->id, taker.id, maker->price, qty});
                taker.quantity -= qty;
                if (qty == maker->quantity) {
                    book_.erase(maker);
                } else {
                    book_.reduce(maker, qty);
                }
            }
            maker = next;
        }
    }
}

bool MatchingEngine::fok_fillable(const NewOrder& req) const {
    const Price limit = effective_limit(req);
    Quantity needed = req.quantity;

    auto scan = [&](const auto& levels) {
        for (const auto& [price, level] : levels) {
            if (!crosses(req.side, limit, price)) break;
            for (const Order* o = level.head; o != nullptr; o = o->next) {
                if (stp_ != StpPolicy::None && o->client == req.client) {
                    // CancelIncoming: our own order would kill the incoming
                    // order mid-walk, making anything behind it unreachable.
                    if (stp_ == StpPolicy::CancelIncoming) return false;
                    continue;  // CancelResting: skipped, contributes nothing
                }
                needed -= o->quantity;
                if (needed <= 0) return true;
            }
        }
        return needed <= 0;
    };
    return req.side == Side::Buy ? scan(book_.asks()) : scan(book_.bids());
}

CancelResult MatchingEngine::cancel(OrderId id) {
    CancelResult res;
    if (!book_.remove(id)) {
        res.reason = RejectReason::UnknownOrder;
        return res;
    }
    res.ok = true;
    return res;
}

ModifyResult MatchingEngine::modify(OrderId id, Price new_price, Quantity new_qty) {
    ModifyResult res;
    Order* o = book_.find(id);
    if (o == nullptr) {
        res.reason = RejectReason::UnknownOrder;
        return res;
    }
    if (new_qty <= 0) {
        res.reason = RejectReason::InvalidQuantity;
        return res;
    }
    if (new_price <= 0) {
        res.reason = RejectReason::InvalidPrice;
        return res;
    }

    if (new_price == o->price && new_qty <= o->quantity) {
        if (new_qty < o->quantity) book_.reduce(o, o->quantity - new_qty);
        res.ok     = true;
        res.status = OrderStatus::Accepted;
        return res;
    }

    // Cancel+replace. Capture fields before remove() recycles the node.
    NewOrder replacement;
    replacement.id       = id;
    replacement.client   = o->client;
    replacement.side     = o->side;
    replacement.type     = OrderType::Limit;
    replacement.tif      = TimeInForce::GTC;
    replacement.price    = new_price;
    replacement.quantity = new_qty;
    book_.remove(id);

    SubmitResult sub = submit(replacement);
    res.ok            = true;
    res.lost_priority = true;
    res.status        = sub.status;
    res.filled        = sub.filled;
    res.trades        = std::move(sub.trades);
    res.stp_cancelled = std::move(sub.stp_cancelled);
    return res;
}

}  // namespace lob
