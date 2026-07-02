#include <catch2/catch_test_macros.hpp>

#include "helpers.hpp"

using namespace lobtest;

TEST_CASE("cancel removes a resting order", "[cancel]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Buy, 100, 10));

    REQUIRE(eng.cancel(1).ok);
    REQUIRE(eng.book().best_bid() == nullptr);

    CancelResult again = eng.cancel(1);
    REQUIRE_FALSE(again.ok);
    REQUIRE(again.reason == RejectReason::UnknownOrder);
}

TEST_CASE("quantity decrease keeps time priority", "[modify]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 10));
    eng.submit(Limit(2, 1, Side::Sell, 100, 10));

    ModifyResult mod = eng.modify(1, 100, 4);
    REQUIRE(mod.ok);
    REQUIRE_FALSE(mod.lost_priority);

    // Order 1 still fills first despite the amend.
    SubmitResult res = eng.submit(Limit(3, 2, Side::Buy, 100, 4));
    REQUIRE(res.trades.size() == 1);
    REQUIRE(res.trades[0].maker_id == 1);
    REQUIRE(res.trades[0].quantity == 4);
    REQUIRE(eng.book().find(1) == nullptr);          // fully filled
    REQUIRE(eng.book().find(2)->quantity == 10);
}

TEST_CASE("quantity increase is cancel+replace and loses priority", "[modify]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Sell, 100, 5));
    eng.submit(Limit(2, 1, Side::Sell, 100, 5));

    ModifyResult mod = eng.modify(1, 100, 8);
    REQUIRE(mod.ok);
    REQUIRE(mod.lost_priority);

    // Order 2 now has priority at the level.
    SubmitResult res = eng.submit(Limit(3, 2, Side::Buy, 100, 5));
    REQUIRE(res.trades[0].maker_id == 2);
    REQUIRE(eng.book().find(1)->quantity == 8);
}

TEST_CASE("price change re-matches like a new order", "[modify]") {
    MatchingEngine eng;
    eng.submit(Limit(1, 1, Side::Buy, 100, 10));
    eng.submit(Limit(2, 2, Side::Sell, 101, 6));

    // Repricing the bid up to 101 crosses the resting ask.
    ModifyResult mod = eng.modify(1, 101, 10);
    REQUIRE(mod.ok);
    REQUIRE(mod.lost_priority);
    REQUIRE(mod.status == OrderStatus::PartiallyFilled);
    REQUIRE(mod.filled == 6);
    REQUIRE(mod.trades.size() == 1);
    REQUIRE(mod.trades[0].maker_id == 2);
    REQUIRE(mod.trades[0].price == 101);
    REQUIRE(eng.book().best_bid()->price == 101);
    REQUIRE(eng.book().best_bid()->total_qty == 4);
    REQUIRE(eng.book().best_ask() == nullptr);
}

TEST_CASE("modify of unknown order fails", "[modify]") {
    MatchingEngine eng;
    ModifyResult mod = eng.modify(99, 100, 10);
    REQUIRE_FALSE(mod.ok);
    REQUIRE(mod.reason == RejectReason::UnknownOrder);
}

TEST_CASE("STP None allows self-trades", "[stp]") {
    MatchingEngine eng(StpPolicy::None);
    eng.submit(Limit(1, 7, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(Limit(2, 7, Side::Buy, 100, 5));
    REQUIRE(res.status == OrderStatus::Filled);
    REQUIRE(res.trades.size() == 1);
}

TEST_CASE("STP CancelResting cancels own maker and keeps matching", "[stp]") {
    MatchingEngine eng(StpPolicy::CancelResting);
    eng.submit(Limit(1, 7, Side::Sell, 100, 5));   // own order, first in queue
    eng.submit(Limit(2, 9, Side::Sell, 100, 5));   // other client behind it

    SubmitResult res = eng.submit(Limit(3, 7, Side::Buy, 100, 8));

    REQUIRE(res.stp_cancelled.size() == 1);
    REQUIRE(res.stp_cancelled[0] == 1);
    REQUIRE(res.trades.size() == 1);
    REQUIRE(res.trades[0].maker_id == 2);          // traded with the other client
    REQUIRE(res.filled == 5);
    REQUIRE(res.status == OrderStatus::PartiallyFilled);
    REQUIRE(eng.book().find(1) == nullptr);        // own maker cancelled
    REQUIRE(eng.book().best_bid()->total_qty == 3);  // remainder rested
}

TEST_CASE("STP CancelIncoming cancels the incoming remainder", "[stp]") {
    MatchingEngine eng(StpPolicy::CancelIncoming);
    eng.submit(Limit(1, 7, Side::Sell, 100, 5));
    eng.submit(Limit(2, 9, Side::Sell, 100, 5));

    SubmitResult res = eng.submit(Limit(3, 7, Side::Buy, 100, 8));

    REQUIRE(res.status == OrderStatus::Cancelled);
    REQUIRE(res.filled == 0);
    REQUIRE(res.trades.empty());
    REQUIRE(eng.book().find(1)->quantity == 5);    // resting order untouched
    REQUIRE(eng.book().best_bid() == nullptr);     // incoming never rested
}

TEST_CASE("FOK feasibility ignores own liquidity under CancelResting", "[stp][tif]") {
    MatchingEngine eng(StpPolicy::CancelResting);
    eng.submit(Limit(1, 7, Side::Sell, 100, 5));   // own — would be cancelled, not filled
    eng.submit(Limit(2, 9, Side::Sell, 100, 5));

    // 8 > 5 reachable from other clients → reject.
    SubmitResult too_big = eng.submit(Limit(3, 7, Side::Buy, 100, 8, TimeInForce::FOK));
    REQUIRE(too_big.status == OrderStatus::Rejected);
    REQUIRE(too_big.reason == RejectReason::FokNotFillable);
    REQUIRE(eng.book().find(1) != nullptr);        // nothing was touched

    // 5 is reachable → fills, cancelling the own maker on the way.
    SubmitResult fits = eng.submit(Limit(4, 7, Side::Buy, 100, 5, TimeInForce::FOK));
    REQUIRE(fits.status == OrderStatus::Filled);
    REQUIRE(fits.trades.size() == 1);
    REQUIRE(fits.trades[0].maker_id == 2);
    REQUIRE(fits.stp_cancelled.size() == 1);
    REQUIRE(fits.stp_cancelled[0] == 1);
}

TEST_CASE("FOK is blocked by own order under CancelIncoming", "[stp][tif]") {
    MatchingEngine eng(StpPolicy::CancelIncoming);
    eng.submit(Limit(1, 7, Side::Sell, 100, 5));   // own order heads the queue
    eng.submit(Limit(2, 9, Side::Sell, 100, 5));

    // Liquidity behind the own order is unreachable: the walk would die there.
    SubmitResult res = eng.submit(Limit(3, 7, Side::Buy, 100, 5, TimeInForce::FOK));
    REQUIRE(res.status == OrderStatus::Rejected);
    REQUIRE(res.reason == RejectReason::FokNotFillable);
}
