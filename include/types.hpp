#pragma once
#include <string>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────
//  types.hpp  —  시스템 전체에서 사용하는 기본 타입 정의
//
//  전략 제작자가 읽어야 할 타입:
//    - OptionBar  : 단일 옵션 심볼의 단일 바 데이터
//    - Order      : 전략이 제출하는 주문
//    - Instrument : 주문 대상 상품 종류
//    - Side       : 매수 / 매도
// ─────────────────────────────────────────────────────────────────

// 상품 종류
enum class Instrument {
    OPTION,   // 바이낸스 옵션 (유러피안)
    FUTURE,   // 선물 (무기한 또는 만기 있는)
    SPOT      // 현물 (BTC)
};

// 방향
enum class Side {
    LONG,     // 매수 / 롱
    SHORT     // 매도 / 숏
};

// ─────────────────────────────────────────────────────────────────
//  OptionBar : 단일 옵션 심볼의 단일 타임스탬프 스냅샷
// ─────────────────────────────────────────────────────────────────
struct OptionBar {
    // 식별자
    std::string symbol;       // "BTC-260125-89000-C"
    int64_t     timestamp;    // Unix milliseconds (UTC)
    int         strike;       // 행사가 (USDT)
    std::string expiry;       // "2026-01-25"
    char        type;         // 'C' (콜) or 'P' (풋)

    // 가격 / 변동성
    double mark_price;        // Binance 마크 프라이스 (USDT)
    double mark_iv;           // 내재 변동성 (소수, 예: 0.85 = 85%)
    double index_price;       // BTC 현물 인덱스 가격 (USDT)

    // Greeks
    double delta;
    double gamma;
    double theta;
    double vega;

    // 호가
    double best_bid;
    double best_ask;

    // 기타
    double open_interest;

    // 편의 메서드
    bool is_call() const { return type == 'C'; }
    bool is_put()  const { return type == 'P'; }
    double mid_price() const {
        if (best_bid > 0 && best_ask > 0)
            return (best_bid + best_ask) / 2.0;
        return mark_price;
    }
};

// ─────────────────────────────────────────────────────────────────
//  FuturesBar : 선물 심볼의 단일 타임스탬프 스냅샷
//  (FuturesFeed → MarketSnapshot 으로 전달)
// ─────────────────────────────────────────────────────────────────
struct FuturesBar {
    std::string symbol;         // "BTCUSDT"
    int64_t     timestamp;      // Unix ms (UTC)

    // OHLCV
    double open;
    double high;
    double low;
    double close;
    double volume;              // base asset volume
    double quote_volume;        // USDT volume

    // 파생 지표 (없으면 0.0)
    double funding_rate;        // 펀딩비 (예: 0.0001 = 0.01%)
    double open_interest;       // 미결제약정 (계약 수)
    double long_ratio;          // 롱 비율 (0~1)
    double short_ratio;         // 숏 비율 (0~1)
    double taker_buy_ratio;     // 시장가 매수/전체 비율 (0~1)

    // 유니버스 플래그
    // compute_universe.py + prepare_futures_data.py 로 생성된 universe.json 기반.
    // true  = 해당 월 top-N 유니버스 포함 → 전략에서 신규 진입 대상
    // false = 유니버스 밖 (이미 보유 포지션 mark-to-market 용도로만 데이터 존재)
    bool is_active = true;

    // 편의 메서드
    double mid_price() const { return (high + low) / 2.0; }
};

// ─────────────────────────────────────────────────────────────────
//  Order : 전략이 on_bar()에서 반환하는 주문
//
//  주의:
//    - quantity는 항상 양수 (방향은 side 필드로 구분)
//    - OPTION 주문 시 strike, option_type, expiry 필수
//    - FUTURE 주문 시 symbol 필드로 심볼 지정 (기본: "BTCUSDT")
// ─────────────────────────────────────────────────────────────────
struct Order {
    Instrument  instrument;
    Side        side;
    double      quantity;     // 수량 (반드시 > 0)

    // OPTION 전용 필드
    int         strike      = 0;
    char        option_type = '\0';   // 'C' or 'P'
    std::string expiry;               // "2026-01-25"

    // FUTURE 전용 필드 (multi-symbol 지원)
    std::string symbol = "BTCUSDT";   // 선물 심볼

    // ── 팩토리 헬퍼 ──────────────────────────────────────────────

    static Order buy_call(int strike_, const std::string& expiry_, double qty) {
        return { Instrument::OPTION, Side::LONG, qty, strike_, 'C', expiry_ };
    }
    static Order sell_call(int strike_, const std::string& expiry_, double qty) {
        return { Instrument::OPTION, Side::SHORT, qty, strike_, 'C', expiry_ };
    }
    static Order buy_put(int strike_, const std::string& expiry_, double qty) {
        return { Instrument::OPTION, Side::LONG, qty, strike_, 'P', expiry_ };
    }
    static Order sell_put(int strike_, const std::string& expiry_, double qty) {
        return { Instrument::OPTION, Side::SHORT, qty, strike_, 'P', expiry_ };
    }
    static Order buy_spot(double qty) {
        return { Instrument::SPOT, Side::LONG, qty, 0, '\0', {} };
    }
    static Order sell_spot(double qty) {
        return { Instrument::SPOT, Side::SHORT, qty, 0, '\0', {} };
    }
    // 선물 매수 (심볼 지정, 기본 BTCUSDT)
    static Order long_future(double qty, const std::string& sym = "BTCUSDT") {
        Order o{ Instrument::FUTURE, Side::LONG, qty, 0, '\0', {}, sym };
        return o;
    }
    // 선물 매도 (심볼 지정, 기본 BTCUSDT)
    static Order short_future(double qty, const std::string& sym = "BTCUSDT") {
        Order o{ Instrument::FUTURE, Side::SHORT, qty, 0, '\0', {}, sym };
        return o;
    }
};

// ─────────────────────────────────────────────────────────────────
//  Position : 현재 보유 포지션 (Portfolio가 관리)
// ─────────────────────────────────────────────────────────────────
struct Position {
    Instrument  instrument;
    Side        side;
    double      quantity;

    // OPTION 전용
    int         strike      = 0;
    char        option_type = '\0';
    std::string expiry;

    // FUTURE 전용 (multi-symbol)
    std::string symbol = "BTCUSDT";

    double      entry_price = 0.0;
    int64_t     entry_time  = 0;
};
