#pragma once
#include "../types.hpp"
#include "live_order_types.hpp"
#include <string>

class OrderMapper {
public:
    // current_position_qty > 0 이면 순롱, < 0 이면 순숏, 0 이면 포지션 없음
    static OrderMapResult map_future_order(const Order& order, double current_position_qty);

    // 체결된 Order 를 로컬 포지션 추정치에 반영하기 위한 signed delta
    static double signed_position_delta(const Order& order);

    static std::string make_client_order_id(const Order& order, int64_t timestamp_ms, int sequence);
};
