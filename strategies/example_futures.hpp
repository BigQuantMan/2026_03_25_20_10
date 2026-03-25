#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include <string>
#include <unordered_map>
#include <deque>
#include <cmath>
#include <iostream>

// ─────────────────────────────────────────────────────────────────
//  FundingMomentumStrategy
//  ──────────────────────────────────────────────────────────────
//  아이디어:
//    - 펀딩비(funding_rate) 가 양수면 롱 포지션 보유자가 숏에게 지급
//      → 시장이 과열되어 있음을 시사 → 숏 진입
//    - 펀딩비 가 음수면 숏 포지션 보유자가 롱에게 지급
//      → 시장이 공포 상태 → 롱 진입
//    - 거래 심볼: BTCUSDT (쉽게 변경 가능)
//
//  파라미터:
//    long_threshold  : 이 값보다 낮은 funding_rate → 롱 진입 (기본: -0.0003)
//    short_threshold : 이 값보다 높은 funding_rate → 숏 진입 (기본: +0.0003)
//    exit_threshold  : 펀딩비가 이 범위 내로 돌아오면 청산 (기본: 0.0001)
//    quantity        : 거래 수량 (기본: 0.01 BTC)
//    symbol          : 대상 심볼 (기본: "BTCUSDT")
//
//  전략 작성 가이드:
//    - snap.futures("BTCUSDT")           → BTCUSDT 선물 바 (없으면 nullptr)
//    - snap.all_futures()                → 모든 심볼의 맵
//    - snap.futures_symbols()            → 심볼 목록
//    - bar->close, bar->open, bar->volume → OHLCV
//    - bar->funding_rate                  → 펀딩비
//    - bar->open_interest                 → 미결제약정
//    - bar->long_ratio / bar->short_ratio → 롱숏 비율
//    - bar->taker_buy_ratio               → 테이커 매수 비율
//    - Order::long_future(qty, "ETHUSDT") → ETH 선물 매수
//    - Order::short_future(qty, "SOLUSDT")→ SOL 선물 매도
// ─────────────────────────────────────────────────────────────────

class FundingMomentumStrategy : public Strategy {
public:
    double      long_threshold  = -0.0001;  // 이 이하 funding_rate → 롱  (음수 펀딩 = 공포)
    double      short_threshold = +0.0001;  // 이 이상 funding_rate → 숏  (양수 펀딩 = 과열)
    double      exit_threshold  =  0.00005; // 이 범위 내면 청산
    double      quantity        =  0.01;    // BTC 단위
    std::string symbol          = "BTCUSDT";

    std::string name() const override {
        return "FundingMomentum";
    }

    void on_start() override {
        position_ = 0;   // 0 = 없음, 1 = 롱, -1 = 숏
        std::cerr << "[FundingMomentum] 시작\n"
                  << "  long_threshold  = " << long_threshold  << "\n"
                  << "  short_threshold = " << short_threshold << "\n"
                  << "  symbol          = " << symbol          << "\n";
    }

    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {
        const FuturesBar* bar = snap.futures(symbol);
        if (!bar) return {};  // 해당 심볼 데이터 없음

        double fr = bar->funding_rate;
        std::vector<Order> orders;

        if (position_ == 0) {
            // ── 신규 진입 ──────────────────────────────────────
            if (fr <= long_threshold) {
                orders.push_back(Order::long_future(quantity, symbol));
                position_ = 1;
            } else if (fr >= short_threshold) {
                orders.push_back(Order::short_future(quantity, symbol));
                position_ = -1;
            }
        } else if (position_ == 1) {
            // ── 롱 포지션 청산 조건 ────────────────────────────
            if (fr > -exit_threshold) {
                orders.push_back(Order::short_future(quantity, symbol));  // 롱 청산
                position_ = 0;
            }
        } else if (position_ == -1) {
            // ── 숏 포지션 청산 조건 ────────────────────────────
            if (fr < exit_threshold) {
                orders.push_back(Order::long_future(quantity, symbol));   // 숏 청산
                position_ = 0;
            }
        }

        return orders;
    }

    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::FUTURE) continue;
            std::cerr << "[FundingMomentum] Fill: "
                      << (o.side == Side::LONG ? "LONG " : "SHORT ")
                      << o.quantity << " " << o.symbol << "\n";
        }
    }

private:
    int position_ = 0;
};


// ─────────────────────────────────────────────────────────────────
//  MultiSymbolMomentumStrategy (고급 예시)
//  ──────────────────────────────────────────────────────────────
//  - 모든 심볼의 최근 N바 수익률을 계산
//  - 상위 K개 롱, 하위 K개 숏 (long-short 포트폴리오)
//  - 1h 바 기준, 24바 룩백
// ─────────────────────────────────────────────────────────────────

class MultiSymbolMomentumStrategy : public Strategy {
public:
    int    lookback = 24;   // 바 수 (1h × 24 = 1일)
    int    top_k    = 3;    // 상위/하위 K개씩
    double quantity = 0.01; // 심볼당 수량

    std::string name() const override { return "MultiSymbolMomentum"; }

    void on_start() override {
        std::cerr << "[MultiSymbolMomentum] lookback=" << lookback
                  << "  top_k=" << top_k << "\n";
    }

    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {
        // 각 심볼의 close 가격 기록
        for (const auto& [sym, bar] : snap.all_futures()) {
            history_[sym].push_back(bar.close);
            if ((int)history_[sym].size() > lookback + 1)
                history_[sym].pop_front();
        }

        // lookback 충분히 쌓이지 않으면 대기
        if (bar_count_++ < lookback) return {};

        // 수익률 계산
        std::vector<std::pair<double, std::string>> returns;
        for (const auto& [sym, prices] : history_) {
            if ((int)prices.size() < lookback + 1) continue;
            double ret = (prices.back() - prices.front()) / (prices.front() + 1e-12);
            returns.emplace_back(ret, sym);
        }
        if (returns.empty()) return {};

        // 정렬
        std::sort(returns.begin(), returns.end());

        std::vector<Order> orders;

        // 1. 기존 포지션 전부 청산
        for (const auto& sym : active_longs_)
            orders.push_back(Order::short_future(quantity, sym));
        for (const auto& sym : active_shorts_)
            orders.push_back(Order::long_future(quantity, sym));
        active_longs_.clear();
        active_shorts_.clear();

        // 2. 하위 top_k 숏 (최악 성과)
        for (int i = 0; i < top_k && i < (int)returns.size(); ++i) {
            orders.push_back(Order::short_future(quantity, returns[i].second));
            active_shorts_.push_back(returns[i].second);
        }

        // 3. 상위 top_k 롱 (최상 성과)
        for (int i = 0; i < top_k; ++i) {
            int idx = (int)returns.size() - 1 - i;
            if (idx < 0) break;
            orders.push_back(Order::long_future(quantity, returns[idx].second));
            active_longs_.push_back(returns[idx].second);
        }

        return orders;
    }

private:
    std::unordered_map<std::string, std::deque<double>> history_;
    std::vector<std::string> active_longs_;
    std::vector<std::string> active_shorts_;
    int bar_count_ = 0;
};
