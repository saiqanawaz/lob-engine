#include <pybind11/pybind11.h>
#include <pybind11/stl.h>

#include <optional>
#include <utility>
#include <vector>

#include "lob/matching_engine.hpp"

namespace py = pybind11;
using namespace lob;

namespace {

// (price, quantity) pairs, best first.
using DepthSide = std::vector<std::pair<Price, Quantity>>;

template <class LevelMap>
DepthSide depth_side(const LevelMap& levels, std::size_t max_levels) {
    DepthSide out;
    out.reserve(std::min(max_levels, levels.size()));
    for (const auto& [price, level] : levels) {
        if (out.size() == max_levels) break;
        out.emplace_back(price, level.total_qty);
    }
    return out;
}

}  // namespace

PYBIND11_MODULE(lob_engine, m) {
    m.doc() = "C++ price-time-priority limit order book matching engine";

    py::enum_<Side>(m, "Side")
        .value("Buy", Side::Buy)
        .value("Sell", Side::Sell);

    py::enum_<OrderType>(m, "OrderType")
        .value("Limit", OrderType::Limit)
        .value("Market", OrderType::Market);

    py::enum_<TimeInForce>(m, "TimeInForce")
        .value("GTC", TimeInForce::GTC)
        .value("IOC", TimeInForce::IOC)
        .value("FOK", TimeInForce::FOK);

    py::enum_<StpPolicy>(m, "StpPolicy")
        .value("None_", StpPolicy::None)
        .value("CancelResting", StpPolicy::CancelResting)
        .value("CancelIncoming", StpPolicy::CancelIncoming);

    py::enum_<OrderStatus>(m, "OrderStatus")
        .value("Accepted", OrderStatus::Accepted)
        .value("PartiallyFilled", OrderStatus::PartiallyFilled)
        .value("Filled", OrderStatus::Filled)
        .value("Cancelled", OrderStatus::Cancelled)
        .value("Rejected", OrderStatus::Rejected);

    py::enum_<RejectReason>(m, "RejectReason")
        .value("None_", RejectReason::None)
        .value("DuplicateId", RejectReason::DuplicateId)
        .value("InvalidQuantity", RejectReason::InvalidQuantity)
        .value("InvalidPrice", RejectReason::InvalidPrice)
        .value("FokNotFillable", RejectReason::FokNotFillable)
        .value("UnknownOrder", RejectReason::UnknownOrder);

    py::class_<Trade>(m, "Trade")
        .def_readonly("maker_id", &Trade::maker_id)
        .def_readonly("taker_id", &Trade::taker_id)
        .def_readonly("price", &Trade::price)
        .def_readonly("quantity", &Trade::quantity)
        .def("__repr__", [](const Trade& t) {
            return "Trade(maker_id=" + std::to_string(t.maker_id) +
                   ", taker_id=" + std::to_string(t.taker_id) +
                   ", price=" + std::to_string(t.price) +
                   ", quantity=" + std::to_string(t.quantity) + ")";
        });

    py::class_<SubmitResult>(m, "SubmitResult")
        .def_readonly("id", &SubmitResult::id)
        .def_readonly("status", &SubmitResult::status)
        .def_readonly("reason", &SubmitResult::reason)
        .def_readonly("filled", &SubmitResult::filled)
        .def_readonly("trades", &SubmitResult::trades)
        .def_readonly("stp_cancelled", &SubmitResult::stp_cancelled);

    py::class_<CancelResult>(m, "CancelResult")
        .def_readonly("ok", &CancelResult::ok)
        .def_readonly("reason", &CancelResult::reason);

    py::class_<ModifyResult>(m, "ModifyResult")
        .def_readonly("ok", &ModifyResult::ok)
        .def_readonly("reason", &ModifyResult::reason)
        .def_readonly("lost_priority", &ModifyResult::lost_priority)
        .def_readonly("status", &ModifyResult::status)
        .def_readonly("filled", &ModifyResult::filled)
        .def_readonly("trades", &ModifyResult::trades)
        .def_readonly("stp_cancelled", &ModifyResult::stp_cancelled);

    py::class_<MatchingEngine>(m, "MatchingEngine")
        .def(py::init<StpPolicy>(), py::arg("stp") = StpPolicy::None)
        .def(
            "submit",
            [](MatchingEngine& eng, OrderId id, Side side, Quantity quantity,
               std::optional<Price> price, OrderType type, TimeInForce tif,
               ClientId client) {
                if (type == OrderType::Limit && !price.has_value()) {
                    throw py::value_error("limit order requires a price");
                }
                NewOrder o;
                o.id       = id;
                o.client   = client;
                o.side     = side;
                o.type     = type;
                o.tif      = tif;
                o.price    = price.value_or(0);
                o.quantity = quantity;
                return eng.submit(o);
            },
            py::arg("id"), py::arg("side"), py::arg("quantity"),
            py::arg("price")  = std::optional<Price>{},
            py::arg("type")   = OrderType::Limit,
            py::arg("tif")    = TimeInForce::GTC,
            py::arg("client") = ClientId{0})
        .def("cancel", &MatchingEngine::cancel, py::arg("id"))
        .def("modify", &MatchingEngine::modify, py::arg("id"), py::arg("price"),
             py::arg("quantity"))
        .def_property_readonly(
            "best_bid",
            [](const MatchingEngine& eng) -> std::optional<std::pair<Price, Quantity>> {
                const PriceLevel* level = eng.book().best_bid();
                if (level == nullptr) return std::nullopt;
                return std::make_pair(level->price, level->total_qty);
            })
        .def_property_readonly(
            "best_ask",
            [](const MatchingEngine& eng) -> std::optional<std::pair<Price, Quantity>> {
                const PriceLevel* level = eng.book().best_ask();
                if (level == nullptr) return std::nullopt;
                return std::make_pair(level->price, level->total_qty);
            })
        .def(
            "depth",
            [](const MatchingEngine& eng, std::size_t levels) {
                return py::make_tuple(depth_side(eng.book().bids(), levels),
                                      depth_side(eng.book().asks(), levels));
            },
            py::arg("levels") = 10,
            "Aggregated (price, quantity) per level: (bids, asks), best first.")
        .def_property_readonly("open_orders", [](const MatchingEngine& eng) {
            return eng.book().open_orders();
        });
}
