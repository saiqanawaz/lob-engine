// Embind wrapper: the same C++ engine the tests and benchmarks run,
// compiled to WebAssembly for the browser demo. No TypeScript port,
// no reimplementation — one matching engine everywhere.

#include <emscripten/bind.h>
#include <emscripten/val.h>

#include <string>

#include "lob/matching_engine.hpp"

using namespace emscripten;
using namespace lob;

namespace {

const char* status_name(OrderStatus s) {
    switch (s) {
        case OrderStatus::Accepted:        return "accepted";
        case OrderStatus::PartiallyFilled: return "partial";
        case OrderStatus::Filled:          return "filled";
        case OrderStatus::Cancelled:       return "cancelled";
        case OrderStatus::Rejected:        return "rejected";
    }
    return "unknown";
}

const char* reason_name(RejectReason r) {
    switch (r) {
        case RejectReason::None:            return "";
        case RejectReason::DuplicateId:     return "duplicate id";
        case RejectReason::InvalidQuantity: return "invalid quantity";
        case RejectReason::InvalidPrice:    return "invalid price";
        case RejectReason::FokNotFillable:  return "FOK not fillable";
        case RejectReason::UnknownOrder:    return "unknown order";
    }
    return "unknown";
}

}  // namespace

class EngineJS {
public:
    val submit(double id, std::string side, double quantity, double price,
               std::string type, std::string tif) {
        NewOrder o;
        o.id       = static_cast<OrderId>(id);
        o.client   = 1;
        o.side     = side == "buy" ? Side::Buy : Side::Sell;
        o.type     = type == "market" ? OrderType::Market : OrderType::Limit;
        o.tif      = tif == "ioc"   ? TimeInForce::IOC
                   : tif == "fok"   ? TimeInForce::FOK
                                    : TimeInForce::GTC;
        o.price    = static_cast<Price>(price);
        o.quantity = static_cast<Quantity>(quantity);

        const SubmitResult r = eng_.submit(o);

        val out = val::object();
        out.set("status", std::string(status_name(r.status)));
        out.set("reason", std::string(reason_name(r.reason)));
        out.set("filled", static_cast<double>(r.filled));
        val trades = val::array();
        int i = 0;
        for (const Trade& t : r.trades) {
            val tv = val::object();
            tv.set("makerId", static_cast<double>(t.maker_id));
            tv.set("price", static_cast<double>(t.price));
            tv.set("quantity", static_cast<double>(t.quantity));
            trades.set(i++, tv);
        }
        out.set("trades", trades);
        return out;
    }

    bool cancel(double id) { return eng_.cancel(static_cast<OrderId>(id)).ok; }

    val depth(int levels) const {
        val out = val::object();
        out.set("bids", side_depth(eng_.book().bids(), levels));
        out.set("asks", side_depth(eng_.book().asks(), levels));
        out.set("openOrders", static_cast<double>(eng_.book().open_orders()));
        return out;
    }

private:
    template <class Map>
    static val side_depth(const Map& levels, int max_levels) {
        val arr = val::array();
        int i = 0;
        for (const auto& [price, level] : levels) {
            if (i >= max_levels) break;
            val row = val::array();
            row.set(0, static_cast<double>(price));
            row.set(1, static_cast<double>(level.total_qty));
            row.set(2, static_cast<double>(level.order_count));
            arr.set(i++, row);
        }
        return arr;
    }

    MatchingEngine eng_;
};

EMSCRIPTEN_BINDINGS(lob_engine) {
    class_<EngineJS>("Engine")
        .constructor<>()
        .function("submit", &EngineJS::submit)
        .function("cancel", &EngineJS::cancel)
        .function("depth", &EngineJS::depth);
}
