#pragma once

#include <algorithm>
#include <cstddef>
#include <random>
#include <vector>

#include "lob/matching_engine.hpp"

// Synthetic order flow for benchmarks. Mirrors the shape of real flow:
// adds dominate, cancels are frequent, marketable orders are rare, and
// prices cluster around a mid so the book develops realistic depth.
namespace lobbench {

using namespace lob;

// Non-crossing flow (single side): isolates pure insert cost.
inline std::vector<NewOrder> make_add_only(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::uniform_int_distribution<Price> price(9000, 11000);
    std::uniform_int_distribution<Quantity> qty(1, 100);

    std::vector<NewOrder> orders;
    orders.reserve(n);
    for (std::size_t i = 0; i < n; ++i) {
        NewOrder o;
        o.id       = static_cast<OrderId>(i + 1);
        o.client   = static_cast<ClientId>(i % 64);
        o.side     = Side::Buy;
        o.type     = OrderType::Limit;
        o.tif      = TimeInForce::GTC;
        o.price    = price(rng);
        o.quantity = qty(rng);
        orders.push_back(o);
    }
    return orders;
}

struct Op {
    enum class Kind : std::uint8_t { Submit, Cancel };
    Kind     kind   = Kind::Submit;
    NewOrder order;      // Submit
    OrderId  target = 0; // Cancel
};

// Mixed flow: ~55% adds, ~35% cancels of random live orders, ~10% market
// orders. Buys sit slightly below the mid and sells slightly above, so the
// distributions overlap and a fraction of limit orders cross on arrival.
inline std::vector<Op> make_mixed(std::size_t n, unsigned seed) {
    std::mt19937_64 rng(seed);
    std::normal_distribution<double> mid(10000.0, 25.0);
    std::uniform_int_distribution<Quantity> limit_qty(1, 100);
    std::uniform_int_distribution<Quantity> market_qty(1, 50);
    std::uniform_real_distribution<double> u(0.0, 1.0);

    std::vector<Op> ops;
    ops.reserve(n);
    std::vector<OrderId> live;
    live.reserve(n);
    OrderId next_id = 1;

    for (std::size_t i = 0; i < n; ++i) {
        const double r = u(rng);
        if (r < 0.55 || live.empty()) {
            Op op;
            op.kind = Op::Kind::Submit;
            NewOrder& o = op.order;
            o.id     = next_id++;
            o.client = static_cast<ClientId>(o.id % 64);
            const bool buy = u(rng) < 0.5;
            o.side     = buy ? Side::Buy : Side::Sell;
            o.type     = OrderType::Limit;
            o.tif      = TimeInForce::GTC;
            o.price    = std::max<Price>(1, static_cast<Price>(mid(rng)) + (buy ? -5 : +5));
            o.quantity = limit_qty(rng);
            live.push_back(o.id);
            ops.push_back(op);
        } else if (r < 0.90) {
            std::uniform_int_distribution<std::size_t> pick(0, live.size() - 1);
            const std::size_t k = pick(rng);
            Op op;
            op.kind   = Op::Kind::Cancel;
            op.target = live[k];  // may already be filled, which is a realistic miss
            live[k] = live.back();
            live.pop_back();
            ops.push_back(op);
        } else {
            Op op;
            op.kind = Op::Kind::Submit;
            NewOrder& o = op.order;
            o.id       = next_id++;
            o.client   = static_cast<ClientId>(o.id % 64);
            o.side     = u(rng) < 0.5 ? Side::Buy : Side::Sell;
            o.type     = OrderType::Market;
            o.tif      = TimeInForce::IOC;
            o.quantity = market_qty(rng);
            ops.push_back(op);
        }
    }
    return ops;
}

inline void apply(MatchingEngine& eng, const Op& op) {
    if (op.kind == Op::Kind::Submit) {
        eng.submit(op.order);
    } else {
        eng.cancel(op.target);
    }
}

}  // namespace lobbench
