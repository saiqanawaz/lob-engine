#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace lobtest;

TEST_CASE("non-crossing limit order rests on the book", "[matching]") {
    MatchingEngine eng;
    SubmitResult res = eng.submit(Limit(1, 1, Side::Buy, 100, 10));

    REQUIRE(res.status == OrderStatus::Accepted);
    REQUIRE(res.filled == 0);
    REQUIRE(res.trades.empty());
    REQUIRE(eng.book().best_bid()->price == 100);
    REQUIRE(eng.book().best_bid()->total_qty == 10);
}

TEST_CASE("crossing buy executes at the maker's price", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 10));
    SubmitResult res = eng.submit(Limit(2, 2, Side::Buy, 105, 10));

    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.filled == 10);
    REQUIRE(res.trades.size() == 1);
    REQUIRE(res.trades[0].maker_id == 1);
    REQUIRE(res.trades[0].taker_id == 2);
    REQUIRE(res.trades[0].price == 100);      // maker price, not taker limit
    REQUIRE(res.trades[0].quantity == 10);
    REQUIRE(eng.book().best_ask() == nullptr);
    REQUIRE(eng.book().open_orders() == 0);
}

TEST_CASE("crossing sell executes at the maker's price", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Buy, 100, 10));
    SubmitResult res = eng.submit(Limit(2, 2, Side::Sell, 95, 4));

    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.trades.size() == 1);
    REQUIRE(res.trades[0].price == 100);
    REQUIRE(eng.book().best_bid()->total_qty == 6);
}

TEST_CASE("partially filled limit order rests its remainder", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    SubmitResult res = eng.submit(Limit(2, 2, Side::Buy, 100, 8));

    REQUIRE(res.status == OrderStatus::PartiallyFilled);
    REQUIRE(res.filled == 5);
    REQUIRE(eng.book().best_ask() == nullptr);
    REQUIRE(eng.book().best_bid()->price == 100);
    REQUIRE(eng.book().best_bid()->total_qty == 3);
    REQUIRE(eng.book().find(2)->quantity == 3);
}

TEST_CASE("taker walks price levels best-first", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 101, 5));
    eng.submit(Limit(2, 1, Side::Sell, 102, 5));
    eng.submit(Limit(3, 1, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(Limit(4, 2, Side::Buy, 102, 12));

    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.trades.size() == 3);
    REQUIRE(res.trades[0].maker_id == 3);     // 100 first
    REQUIRE(res.trades[0].price == 100);
    REQUIRE(res.trades[1].maker_id == 1);     // then 101
    REQUIRE(res.trades[1].price == 101);
    REQUIRE(res.trades[2].maker_id == 2);     // then 102, partial
    REQUIRE(res.trades[2].quantity == 2);
    REQUIRE(eng.book().best_ask()->price == 102);
    REQUIRE(eng.book().best_ask()->total_qty == 3);
}

TEST_CASE("makers at the same price fill in FIFO order", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    eng.submit(Limit(2, 1, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(Limit(3, 2, Side::Buy, 100, 7));

    REQUIRE(res.trades.size() == 2);
    REQUIRE(res.trades[0].maker_id == 1);
    REQUIRE(res.trades[0].quantity == 5);
    REQUIRE(res.trades[1].maker_id == 2);
    REQUIRE(res.trades[1].quantity == 2);
    REQUIRE(eng.book().find(2)->quantity == 3);
}

TEST_CASE("market order sweeps levels and cancels its residual", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    eng.submit(Limit(2, 1, Side::Sell, 101, 5));

    SubmitResult res = eng.submit(MarketOrder(3, 2, Side::Buy, 15));

    REQUIRE(res.status == OrderStatus::Cancelled);  // residual 5 cancelled
    REQUIRE(res.filled == 10);
    REQUIRE(res.trades.size() == 2);
    REQUIRE(eng.book().best_ask() == nullptr);
    REQUIRE(eng.book().best_bid() == nullptr);      // nothing rested
}

TEST_CASE("market order on an empty book cancels with zero fill", "[matching]") {
    MatchingEngine eng;
    SubmitResult res = eng.submit(MarketOrder(1, 1, Side::Sell, 10));

    REQUIRE(res.status == OrderStatus::Cancelled);
    REQUIRE(res.filled == 0);
    REQUIRE(res.trades.empty());
}

TEST_CASE("duplicate open order id is rejected", "[matching]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Buy, 100, 10));
    SubmitResult res = eng.submit(Limit(1, 1, Side::Buy, 99, 5));

    REQUIRE(res.status == OrderStatus::Rejected);
    REQUIRE(res.reason == RejectReason::DuplicateId);
    REQUIRE(eng.book().open_orders() == 1);
}

TEST_CASE("invalid quantity and price are rejected", "[matching]") {
    MatchingEngine eng;

    SubmitResult bad_qty = eng.submit(Limit(1, 1, Side::Buy, 100, 0));
    REQUIRE(bad_qty.status == OrderStatus::Rejected);
    REQUIRE(bad_qty.reason == RejectReason::InvalidQuantity);

    SubmitResult bad_px = eng.submit(Limit(2, 1, Side::Buy, 0, 10));
    REQUIRE(bad_px.status == OrderStatus::Rejected);
    REQUIRE(bad_px.reason == RejectReason::InvalidPrice);

    REQUIRE(eng.book().open_orders() == 0);
}
