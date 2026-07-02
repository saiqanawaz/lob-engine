#pragma once

#include "lob/matching_engine.hpp"

namespace lobtest {

using namespace lob;

inline NewOrder Limit(OrderId id, ClientId client, Side side, Price px, Quantity qty,
                      TimeInForce tif = TimeInForce::GTC) {
    NewOrder o;
    o.id       = id;
    o.client   = client;
    o.side     = side;
    o.type     = OrderType::Limit;
    o.tif      = tif;
    o.price    = px;
    o.quantity = qty;
    return o;
}

inline NewOrder MarketOrder(OrderId id, ClientId client, Side side, Quantity qty,
                            TimeInForce tif = TimeInForce::IOC) {
    NewOrder o;
    o.id       = id;
    o.client   = client;
    o.side     = side;
    o.type     = OrderType::Market;
    o.tif      = tif;
    o.price    = 0;
    o.quantity = qty;
    return o;
}

}  // namespace lobtest
