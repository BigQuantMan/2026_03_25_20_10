#include "../../include/live/order_mapper.hpp"
#include <sstream>

OrderMapResult OrderMapper::map_future_order(const Order& order, double current_position_qty) {
    OrderMapResult result;

    if (order.instrument != Instrument::FUTURE) {
        result.reason = "Only FUTURE orders are supported by the live bridge.";
        return result;
    }
    if (order.quantity <= 0.0) {
        result.reason = "order.quantity must be > 0";
        return result;
    }

    result.supported = true;
    result.request.symbol = order.symbol;
    result.request.type = "MARKET";
    result.request.quantity = order.quantity;

    if (order.side == Side::LONG) {
        result.request.side = "BUY";
        result.request.reduce_only = (current_position_qty < 0.0);
    } else {
        result.request.side = "SELL";
        result.request.reduce_only = (current_position_qty > 0.0);
    }

    return result;
}

double OrderMapper::signed_position_delta(const Order& order) {
    return (order.side == Side::LONG) ? order.quantity : -order.quantity;
}

std::string OrderMapper::make_client_order_id(const Order& order, int64_t timestamp_ms, int sequence) {
    std::ostringstream oss;
    oss << "live_" << order.symbol << "_" << timestamp_ms << "_" << sequence;
    return oss.str();
}
