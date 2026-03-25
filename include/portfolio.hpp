#pragma once
#include "types.hpp"
#include "market_data.hpp"
#include <vector>
#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────────────────────────
//  portfolio.hpp  —  포지션·현금·PnL 관리
// ─────────────────────────────────────────────────────────────────

struct PortfolioState {
    int64_t  timestamp;
    double   cash;             // 보유 현금 (USDT)
    double   position_value;   // 포지션 시가 (USDT)
    double   total_value;      // 초기자본 + 실현손익 + 미실현손익 (metrics용)
    double   realized_value;   // 초기자본 + 실현손익만 (equity curve용)
    double   realized_pnl;
    double   unrealized_pnl;

    // 일별 거래 집계 (daily turnover 계산용)
    double   daily_traded_notional = 0.0;
};

// 체결 기록 (결과 파일에 저장)
struct Trade {
    int64_t    timestamp;
    Instrument instrument;
    Side       side;
    double     quantity;
    double     price;

    // OPTION 전용
    int         strike      = 0;
    char        option_type = '\0';
    std::string expiry;

    // FUTURE 전용
    std::string symbol;              // "BTCUSDT" 등

    double      pnl         = 0.0;   // 청산 시 실현 손익
};

class Portfolio {
public:
    explicit Portfolio(double initial_capital);

    // ── Backtester가 호출 ──────────────────────────────────────

    // 주문 체결 (t+1 봉 open 가격으로 체결 — look-ahead 완전 차단)
    // → 전략이 t 봉 close까지 보고 주문 → t+1 봉이 열리는 즉시 open 가격으로 체결
    // 반환: 실제로 체결된 주문 (invalid 주문은 제외 후 반환)
    std::vector<Order> fill_orders(
        const std::vector<Order>& orders,
        const MarketSnapshot&     snap,   // t+1 스냅샷
        int64_t                   ts
    );

    // 포트폴리오 시가 업데이트 (매 바마다 호출)
    void mark_to_market(const MarketSnapshot& snap, int64_t ts);

    // ── 상태 조회 ─────────────────────────────────────────────

    const PortfolioState&        state()     const { return state_; }
    const std::vector<Position>& positions() const { return positions_; }
    const std::vector<Trade>&    trades()    const { return trades_; }

    // 특정 옵션 포지션 조회 (없으면 nullptr)
    const Position* find_option(int strike, char type, const std::string& expiry) const;
    double          spot_position() const;   // 현물 포지션 크기 (음수 = 숏)
    double          future_position() const; // 선물 포지션 크기

    // 일별 turnover 리셋 (DataFeed 날짜 변경 시 호출)
    void reset_daily();

private:
    double                initial_capital_;
    PortfolioState         state_;
    std::vector<Position>  positions_;
    std::vector<Trade>     trades_;

    // 체결 가격 조회 (bid/ask mid 사용)
    double get_fill_price(const Order& order, const MarketSnapshot& snap) const;

    // 포지션 추가 / 감소
    void   add_position(const Order& order, double fill_price, int64_t ts);
    // found: 매칭 포지션이 실제로 존재했는지 여부
    // → PnL = 0.0 이어도 found = true 일 수 있음 (브레이크이븐)
    double close_position(const Order& order, double fill_price, int64_t ts, bool& found);
};
