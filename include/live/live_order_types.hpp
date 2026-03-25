#pragma once
#include <string>

// ─────────────────────────────────────────────────────────────────
//  live_order_types.hpp
//  - 실주문 제출에 필요한 최소 자료형
//  - 기존 백테스트용 Order 와 분리하여 관리
// ─────────────────────────────────────────────────────────────────

struct LiveOrderRequest {
    std::string symbol;            // 예: BTCUSDT
    std::string side;              // BUY / SELL
    std::string type = "MARKET";   // 현재 예제는 MARKET 위주
    double      quantity = 0.0;
    bool        reduce_only = false;
    std::string client_order_id;
};

struct ExecutionReport {
    bool        accepted = false;   // 거래소가 요청을 수락했는가
    bool        filled   = false;   // 로컬 엔진이 fill 로 간주하는가
    int         http_status = 0;
    std::string venue = "BINANCE_FUTURES";
    std::string venue_order_id;
    std::string client_order_id;
    std::string raw_response;
    std::string error_message;
};

struct OrderMapResult {
    bool             supported = false;
    LiveOrderRequest request;
    std::string      reason;
};
