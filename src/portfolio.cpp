#include "../include/portfolio.hpp"
#include <stdexcept>
#include <cmath>
#include <algorithm>

Portfolio::Portfolio(double initial_capital)
    : initial_capital_(initial_capital)
{
    state_.cash           = initial_capital;
    state_.position_value = 0.0;
    state_.total_value    = initial_capital;
    state_.realized_pnl   = 0.0;
    state_.unrealized_pnl = 0.0;
    state_.timestamp      = 0;
}

// ── 체결 가격 조회 ────────────────────────────────────────────────
double Portfolio::get_fill_price(const Order& order, const MarketSnapshot& snap) const {
    if (order.instrument == Instrument::SPOT) {
        return snap.spot_price();
    }
    if (order.instrument == Instrument::FUTURE) {
        // 멀티심볼: FuturesFeed 모드면 해당 심볼 open 가격 사용
        // → t 시점 신호 → t+1 봉 open 체결 (1분봉 딜레이 제거)
        const FuturesBar* fb = snap.futures(order.symbol);
        if (fb && fb->open > 0) return fb->open;
        return snap.spot_price();  // fallback: Options 모드
    }
    // OPTION
    auto opt = snap.get(order.strike, order.option_type, order.expiry);
    if (!opt) return 0.0;

    // 매수 → ask 가격, 매도 → bid 가격 (스프레드 반영)
    if (order.side == Side::LONG) {
        return (opt->best_ask > 0) ? opt->best_ask : opt->mark_price;
    } else {
        return (opt->best_bid > 0) ? opt->best_bid : opt->mark_price;
    }
}

// ── 포지션 추가 ───────────────────────────────────────────────────
void Portfolio::add_position(const Order& order, double fill_price, int64_t ts) {
    // 기존 같은 포지션이 있으면 평균 단가로 합산
    for (auto& pos : positions_) {
        bool same_instrument = (pos.instrument == order.instrument);
        bool same_side       = (pos.side == order.side);
        bool same_option     = (order.instrument != Instrument::OPTION) ||
                               (pos.strike == order.strike &&
                                pos.option_type == order.option_type &&
                                pos.expiry == order.expiry);
        // FUTURE: 심볼까지 일치해야 같은 포지션
        bool same_future_sym = (order.instrument != Instrument::FUTURE) ||
                               (pos.symbol == order.symbol);

        if (same_instrument && same_side && same_option && same_future_sym) {
            double total_qty = pos.quantity + order.quantity;
            pos.entry_price  = (pos.entry_price * pos.quantity +
                                fill_price * order.quantity) / total_qty;
            pos.quantity     = total_qty;
            return;
        }
    }

    // 새 포지션 추가
    Position pos;
    pos.instrument   = order.instrument;
    pos.side         = order.side;
    pos.quantity     = order.quantity;
    pos.strike       = order.strike;
    pos.option_type  = order.option_type;
    pos.expiry       = order.expiry;
    pos.symbol       = order.symbol;   // 선물 심볼 복사
    pos.entry_price  = fill_price;
    pos.entry_time   = ts;
    positions_.push_back(pos);
}

// ── 포지션 감소 / 청산 → 실현 손익 반환 ─────────────────────────
// found : 매칭 포지션이 존재했으면 true (PnL = 0.0 인 브레이크이븐 포함)
double Portfolio::close_position(const Order& order, double fill_price, int64_t /*ts*/, bool& found) {
    found = false;
    for (size_t i = 0; i < positions_.size(); ++i) {
        auto& pos = positions_[i];
        Side  close_side = (order.side == Side::LONG) ? Side::SHORT : Side::LONG;
        bool  match = (pos.instrument == order.instrument &&
                       pos.side       == close_side);

        if (order.instrument == Instrument::OPTION) {
            match = match &&
                    pos.strike      == order.strike &&
                    pos.option_type == order.option_type &&
                    pos.expiry      == order.expiry;
        }

        // 선물: 심볼까지 매칭
        if (order.instrument == Instrument::FUTURE) {
            match = match && pos.symbol == order.symbol;
        }

        if (!match) continue;

        found = true;   // 포지션 찾음 (PnL 과 무관)

        double close_qty  = std::min(order.quantity, pos.quantity);
        double pnl        = 0.0;
        Side   pos_side   = pos.side;          // erase 전에 미리 복사
        double entry_px   = pos.entry_price;

        if (pos_side == Side::LONG)
            pnl = (fill_price - entry_px) * close_qty;
        else
            pnl = (entry_px   - fill_price) * close_qty;

        pos.quantity -= close_qty;
        if (pos.quantity < 1e-9)
            positions_.erase(positions_.begin() + i);

        state_.realized_pnl += pnl;

        // 현금 회수:
        //   LONG  청산: 매도 대금 = fill_price × qty
        //   SHORT 청산: 담보(원금) + PnL, 단 0 미만으로는 내려가지 않음
        //               (가격이 2배 이상 상승해도 최대 손실 = 담보금액 ← 강제청산 시뮬)
        if (pos_side == Side::LONG)
            state_.cash += fill_price * close_qty;
        else
            state_.cash += std::max(0.0, entry_px * close_qty + pnl);
        return pnl;
    }
    return 0.0;
}

// ── 주문 체결 ─────────────────────────────────────────────────────
std::vector<Order> Portfolio::fill_orders(
    const std::vector<Order>& orders,
    const MarketSnapshot&     snap,
    int64_t                   ts)
{
    std::vector<Order> filled;

    for (const auto& order : orders) {
        if (order.quantity <= 0) continue;

        double price = get_fill_price(order, snap);
        if (price <= 0) continue;

        double notional   = price * order.quantity;
        double close_pnl  = 0.0;   // 청산 시 실현 손익 (거래 기록에 사용)
        bool   is_close   = false;  // 청산 주문이었는지 여부

        // ── LONG 주문: 우선 기존 SHORT 청산 시도, 없으면 신규 롱 오픈 ──
        if (order.side == Side::LONG) {
            bool found_close = false;
            close_pnl = close_position(order, price, ts, found_close);

            if (found_close) {
                // 기존 숏 포지션 청산 완료 (브레이크이븐 포함)
                is_close = true;
            } else {
                // 매칭 포지션 없음 → 신규 롱 오픈
                if (state_.cash < notional) continue;
                state_.cash -= notional;
                add_position(order, price, ts);
            }

        // ── SHORT 주문: 우선 기존 LONG 청산 시도, 없으면 신규 숏 오픈 ──
        } else {
            bool found_close = false;
            close_pnl = close_position(order, price, ts, found_close);

            if (found_close) {
                // 기존 롱 포지션 청산 완료 (브레이크이븐 포함)
                is_close = true;
            } else {
                // 매칭 포지션 없음 → 신규 숏 포지션 오픈
                if (order.instrument == Instrument::OPTION) {
                    // 옵션 숏: 프리미엄 수취 (담보 없이 현금 증가)
                    state_.cash += notional;
                    add_position(order, price, ts);
                } else {
                    // FUTURE / SPOT 숏: 담보(notional) 예치 후 숏 진입
                    if (state_.cash < notional) continue;
                    state_.cash -= notional;
                    add_position(order, price, ts);
                }
            }
        }

        // 거래 기록 (청산이면 pnl 포함)
        Trade t;
        t.timestamp   = ts;
        t.instrument  = order.instrument;
        t.side        = order.side;
        t.quantity    = order.quantity;
        t.price       = price;
        t.pnl         = is_close ? close_pnl : 0.0;
        t.strike      = order.strike;
        t.option_type = order.option_type;
        t.expiry      = order.expiry;
        t.symbol      = order.symbol;   // FUTURE 심볼 ("BTCUSDT" 등)
        trades_.push_back(t);

        state_.daily_traded_notional += notional;
        filled.push_back(order);
    }

    return filled;
}

// ── 시가 평가 ─────────────────────────────────────────────────────
void Portfolio::mark_to_market(const MarketSnapshot& snap, int64_t ts) {
    state_.timestamp      = ts;
    state_.unrealized_pnl = 0.0;
    double pos_value      = 0.0;

    for (const auto& pos : positions_) {
        double current_price = 0.0;

        if (pos.instrument == Instrument::OPTION) {
            auto opt = snap.get(pos.strike, pos.option_type, pos.expiry);
            current_price = opt ? opt->mark_price : pos.entry_price;
        } else if (pos.instrument == Instrument::SPOT) {
            current_price = snap.spot_price();
        } else if (pos.instrument == Instrument::FUTURE) {
            // 멀티심볼: FuturesFeed 모드면 해당 심볼 close 가격 사용
            const FuturesBar* fb = snap.futures(pos.symbol);
            current_price = (fb && fb->close > 0) ? fb->close : snap.spot_price();
        }

        double value = current_price * pos.quantity;
        double upnl  = (pos.side == Side::LONG)
                       ? (current_price - pos.entry_price) * pos.quantity
                       : (pos.entry_price - current_price) * pos.quantity;

        pos_value             += (pos.side == Side::LONG ? value : -value);
        state_.unrealized_pnl += upnl;
    }

    state_.position_value = pos_value;
    // total_value = 초기자본 + 실현손익 + 미실현손익 (metrics용)
    state_.total_value    = initial_capital_ + state_.realized_pnl + state_.unrealized_pnl;
    // realized_value = 초기자본 + 실현손익만 (equity curve용, 미청산 포지션 제외)
    state_.realized_value = initial_capital_ + state_.realized_pnl;
}

void Portfolio::reset_daily() {
    state_.daily_traded_notional = 0.0;
}

const Position* Portfolio::find_option(int strike, char type, const std::string& expiry) const {
    for (const auto& pos : positions_)
        if (pos.instrument == Instrument::OPTION &&
            pos.strike == strike && pos.option_type == type && pos.expiry == expiry)
            return &pos;
    return nullptr;
}

double Portfolio::spot_position() const {
    double total = 0.0;
    for (const auto& pos : positions_)
        if (pos.instrument == Instrument::SPOT)
            total += (pos.side == Side::LONG ? pos.quantity : -pos.quantity);
    return total;
}

double Portfolio::future_position() const {
    double total = 0.0;
    for (const auto& pos : positions_)
        if (pos.instrument == Instrument::FUTURE)
            total += (pos.side == Side::LONG ? pos.quantity : -pos.quantity);
    return total;
}
