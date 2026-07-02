#pragma once

#include <cstdint>

namespace lob {

using OrderId  = std::uint64_t;
using ClientId = std::uint32_t;
using Price    = std::int64_t;   // integer ticks; no floating point inside the engine
using Quantity = std::int64_t;
using SeqNum   = std::uint64_t;  // arrival sequence number

enum class Side : std::uint8_t { Buy, Sell };
enum class OrderType : std::uint8_t { Limit, Market };
enum class TimeInForce : std::uint8_t { GTC, IOC, FOK };

inline Side opposite(Side s) { return s == Side::Buy ? Side::Sell : Side::Buy; }

struct PriceLevel;  // defined in order_book.hpp

// A resting order. Doubles as an intrusive doubly-linked node in its price
// level's FIFO queue. Pool-allocated by OrderBook; never moves in memory
// while resting, so raw pointers to it stay valid until it is removed.
struct Order {
    OrderId     id       = 0;
    ClientId    client   = 0;
    Side        side     = Side::Buy;
    TimeInForce tif      = TimeInForce::GTC;
    Price       price    = 0;
    Quantity    quantity = 0;  // remaining open quantity
    SeqNum      seq      = 0;

    Order*      prev  = nullptr;
    Order*      next  = nullptr;
    PriceLevel* level = nullptr;
};

}  // namespace lob
