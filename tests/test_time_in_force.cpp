#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace lobtest;

TEST_CASE("IOC fills fully when liquidity exists", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 10));

    SubmitResult res = eng.submit(Limit(2, 2, Side::Buy, 100, 10, TimeInForce::IOC));

    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.filled == 10);
}

TEST_CASE("IOC fills what it can and cancels the residual", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(Limit(2, 2, Side::Buy, 100, 8, TimeInForce::IOC));

    REQUIRE(res.status == OrderStatus::Cancelled);
    REQUIRE(res.filled == 5);
    REQUIRE(eng.book().best_bid() == nullptr);      // residual never rested
    REQUIRE(eng.book().open_orders() == 0);
}

TEST_CASE("IOC with no crossing liquidity cancels untouched", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 105, 5));

    SubmitResult res = eng.submit(Limit(2, 2, Side::Buy, 100, 8, TimeInForce::IOC));

    REQUIRE(res.status == OrderStatus::Cancelled);
    REQUIRE(res.filled == 0);
    REQUIRE(res.trades.empty());
    REQUIRE(eng.book().best_ask()->total_qty == 5);  // maker untouched
}

TEST_CASE("FOK fills fully across multiple levels", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    eng.submit(Limit(2, 1, Side::Sell, 101, 5));

    SubmitResult res = eng.submit(Limit(3, 2, Side::Buy, 101, 10, TimeInForce::FOK));

    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.filled == 10);
    REQUIRE(res.trades.size() == 2);
    REQUIRE(eng.book().open_orders() == 0);
}

TEST_CASE("FOK with insufficient liquidity rejects without touching the book", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    eng.submit(Limit(2, 1, Side::Sell, 101, 5));

    SubmitResult res = eng.submit(Limit(3, 2, Side::Buy, 100, 8, TimeInForce::FOK));

    REQUIRE(res.status == OrderStatus::Rejected);
    REQUIRE(res.reason == RejectReason::FokNotFillable);
    REQUIRE(res.trades.empty());
    REQUIRE(eng.book().find(1)->quantity == 5);      // makers untouched
    REQUIRE(eng.book().find(2)->quantity == 5);
}

TEST_CASE("FOK market order rejects when the whole book is too thin", "[tif]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(MarketOrder(2, 2, Side::Buy, 8, TimeInForce::FOK));

    REQUIRE(res.status == OrderStatus::Rejected);
    REQUIRE(res.reason == RejectReason::FokNotFillable);
    REQUIRE(eng.book().best_ask()->total_qty == 5);
}
