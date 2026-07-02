#include <catch2/catch_test_macros.hpp>

#include "lob/order_book.hpp"

using namespace lob;

namespace {

Order make(OrderId id, Side side, Price px, Quantity qty) {
    Order o;
    o.id       = id;
    o.client   = 1;
    o.side     = side;
    o.price    = px;
    o.quantity = qty;
    return o;
}

}  // namespace

TEST_CASE("insert creates levels and updates best bid/ask", "[book]") {
    OrderBook book;
    REQUIRE(book.best_bid() == nullptr);
    REQUIRE(book.best_ask() == nullptr);

    book.insert(make(1, Side::Buy, 100, 10));
    book.insert(make(2, Side::Buy, 102, 5));
    book.insert(make(3, Side::Sell, 105, 7));
    book.insert(make(4, Side::Sell, 104, 3));

    REQUIRE(book.best_bid()->price == 102);   // highest bid first
    REQUIRE(book.best_bid()->total_qty == 5);
    REQUIRE(book.best_ask()->price == 104);   // lowest ask first
    REQUIRE(book.best_ask()->total_qty == 3);
    REQUIRE(book.open_orders() == 4);
}

TEST_CASE("duplicate id insert is refused", "[book]") {
    OrderBook book;
    REQUIRE(book.insert(make(1, Side::Buy, 100, 10)) != nullptr);
    REQUIRE(book.insert(make(1, Side::Buy, 101, 5)) == nullptr);
    REQUIRE(book.open_orders() == 1);
}

TEST_CASE("orders at the same price queue FIFO", "[book]") {
    OrderBook book;
    book.insert(make(1, Side::Buy, 100, 10));
    book.insert(make(2, Side::Buy, 100, 20));
    book.insert(make(3, Side::Buy, 100, 30));

    const PriceLevel* level = book.best_bid();
    REQUIRE(level->order_count == 3);
    REQUIRE(level->total_qty == 60);
    REQUIRE(level->head->id == 1);            // oldest matches first
    REQUIRE(level->head->next->id == 2);
    REQUIRE(level->tail->id == 3);
}

TEST_CASE("removing a middle order relinks the queue", "[book]") {
    OrderBook book;
    book.insert(make(1, Side::Sell, 100, 10));
    book.insert(make(2, Side::Sell, 100, 20));
    book.insert(make(3, Side::Sell, 100, 30));

    REQUIRE(book.remove(2));
    const PriceLevel* level = book.best_ask();
    REQUIRE(level->order_count == 2);
    REQUIRE(level->total_qty == 40);
    REQUIRE(level->head->id == 1);
    REQUIRE(level->head->next->id == 3);
    REQUIRE(level->tail->id == 3);
    REQUIRE(level->tail->prev->id == 1);
    REQUIRE(book.find(2) == nullptr);
}

TEST_CASE("empty level is erased and best price falls back", "[book]") {
    OrderBook book;
    book.insert(make(1, Side::Buy, 102, 5));
    book.insert(make(2, Side::Buy, 100, 10));

    REQUIRE(book.best_bid()->price == 102);
    REQUIRE(book.remove(1));
    REQUIRE(book.best_bid()->price == 100);
    REQUIRE(book.bids().size() == 1);
}

TEST_CASE("remove of unknown id fails cleanly", "[book]") {
    OrderBook book;
    REQUIRE_FALSE(book.remove(42));
}

TEST_CASE("reduce lowers quantity in place and keeps position", "[book]") {
    OrderBook book;
    book.insert(make(1, Side::Buy, 100, 10));
    book.insert(make(2, Side::Buy, 100, 20));

    Order* o = book.find(1);
    book.reduce(o, 4);
    REQUIRE(o->quantity == 6);
    const PriceLevel* level = book.best_bid();
    REQUIRE(level->total_qty == 26);
    REQUIRE(level->head->id == 1);            // still first in the queue
}
