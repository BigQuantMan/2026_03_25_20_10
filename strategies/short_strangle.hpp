#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include <string>
#include <cmath>
#include <iostream>

// ─────────────────────────────────────────────────────────────────
//  ShortStrangleStrategy  (OTM 숏 스트랭글 — 순수 옵션 전략)
//  ──────────────────────────────────────────────────────────────
//  아이디어:
//    IV가 높을 때 OTM 콜 + OTM 풋을 동시에 매도 (프리미엄 수취)
//    가격이 좁은 범위 내에 머물면 이익, 크게 움직이면 손실
//
//  진입 조건:
//    ATM 콜·풋 평균 IV > iv_entry_threshold  AND  잔존기간 > days_to_expiry_exit 일
//
//  청산 조건 (셋 중 하나):
//    1. 이익목표:  현재 포지션 가치 ≤ 수취 프리미엄 × (1 − profit_target_pct)
//    2. 손절:      현재 포지션 가치 ≥ 수취 프리미엄 × (1 + stop_loss_pct)
//    3. 만기 임박: 잔존기간 ≤ days_to_expiry_exit 일
//
//  파라미터:
//    iv_entry_threshold  : 진입 최소 IV (기본: 0.60 = 60%)
//    otm_offset          : ATM 기준 OTM 행사가 간격 USD (기본: 500)
//    position_qty        : 계약 수량 (기본: 0.01 BTC)
//    profit_target_pct   : 수취 프리미엄 대비 이익 목표 비율 (기본: 0.50 = 50%)
//    stop_loss_pct       : 수취 프리미엄 대비 손절 비율    (기본: 2.00 = 200%)
//    days_to_expiry_exit : 청산할 잔존일 (기본: 3일)
//    target_expiry       : "" = front month 자동 선택
//
//  ※ 데이터: 옵션 데이터 필수
//       POST /api/prepare-options  또는
//       python3 tools/prepare_data.py
// ─────────────────────────────────────────────────────────────────

class ShortStrangleStrategy : public Strategy {
public:
    // ── 파라미터 ──────────────────────────────────────────────────
    double      iv_entry_threshold  = 0.60;  // 진입 최소 IV (60%)
    int         otm_offset          = 500;   // ATM ± 오프셋 (USD)
    double      position_qty        = 0.01;  // 계약 수량 (BTC)
    double      profit_target_pct   = 0.50;  // 50% 이익 목표
    double      stop_loss_pct       = 2.00;  // 200% 손절
    double      days_to_expiry_exit = 3.0;   // 잔존 3일 이하 시 청산
    std::string target_expiry       = "";    // "" = front month

    std::string name() const override { return "ShortStrangle"; }

    // ── 초기화 ────────────────────────────────────────────────────
    void on_start() override {
        position_      = 0;
        entry_premium_ = 0.0;
        call_strike_   = 0;
        put_strike_    = 0;
        expiry_        = "";

        std::cerr << "[ShortStrangle] 시작\n"
                  << "  iv_entry_threshold  = " << (iv_entry_threshold * 100) << "%\n"
                  << "  otm_offset          = ±$" << otm_offset << "\n"
                  << "  profit_target_pct   = " << (profit_target_pct  * 100) << "%\n"
                  << "  stop_loss_pct       = " << (stop_loss_pct      * 100) << "%\n"
                  << "  days_to_expiry_exit = " << days_to_expiry_exit << "일\n";
    }

    // ── 매 바 처리 ────────────────────────────────────────────────
    std::vector<Order> on_bar(int64_t ts, const MarketSnapshot& snap) override {
        std::vector<Order> orders;

        // 만기 결정
        const std::string expiry = target_expiry.empty()
                                   ? snap.front_expiry()
                                   : target_expiry;
        if (expiry.empty()) return orders;

        // ATM 행사가
        auto atm_k = snap.atm_strike(expiry);
        if (!atm_k) return orders;

        const int atm    = *atm_k;
        const int call_k = atm + otm_offset;  // OTM 콜
        const int put_k  = atm - otm_offset;  // OTM 풋

        const double T = time_to_expiry(ts, expiry);

        // ── 포지션 없음: 진입 조건 체크 ──────────────────────────
        if (position_ == 0) {
            auto call_opt = snap.get(call_k, 'C', expiry);
            auto put_opt  = snap.get(put_k,  'P', expiry);
            if (!call_opt || !put_opt) return orders;

            const double call_iv = call_opt->mark_iv;
            const double put_iv  = put_opt->mark_iv;
            const double call_px = call_opt->mark_price;
            const double put_px  = put_opt->mark_price;

            if (call_iv <= 0 || put_iv <= 0) return orders;
            if (call_px <= 0 || put_px <= 0) return orders;

            const double avg_iv = (call_iv + put_iv) / 2.0;

            // IV 임계치 초과 + 충분한 잔존기간
            if (avg_iv >= iv_entry_threshold &&
                T > days_to_expiry_exit / 365.0)
            {
                // ── 숏 스트랭글 진입 ──────────────────────────
                orders.push_back(Order::sell_call(call_k, expiry, position_qty));
                orders.push_back(Order::sell_put(put_k,  expiry, position_qty));

                entry_premium_ = (call_px + put_px) * position_qty;
                call_strike_   = call_k;
                put_strike_    = put_k;
                expiry_        = expiry;
                position_      = -1;  // 숏

                std::cerr << "[ShortStrangle] 진입: CALL " << call_k
                          << " PUT " << put_k
                          << " IV=" << (avg_iv * 100.0) << "%"
                          << " 프리미엄=" << entry_premium_ << " USDT\n";
            }

        // ── 포지션 있음: 청산 조건 체크 ──────────────────────────
        } else {
            bool   should_exit = false;
            const char* exit_reason = "";

            // 1. 만기 임박 청산
            if (T <= days_to_expiry_exit / 365.0) {
                should_exit = true;
                exit_reason = "만기 임박";
            }

            // 2. 이익목표 / 손절 (현재 가치 조회 가능할 때)
            if (!should_exit) {
                auto call_opt = snap.get(call_strike_, 'C', expiry_);
                auto put_opt  = snap.get(put_strike_,  'P', expiry_);

                if (call_opt && put_opt) {
                    const double cur_val =
                        (call_opt->mark_price + put_opt->mark_price) * position_qty;

                    // 이익목표: 현재 가치가 수취 프리미엄의 (1-target) 이하
                    if (cur_val <= entry_premium_ * (1.0 - profit_target_pct)) {
                        should_exit = true;
                        exit_reason = "이익목표";
                    }
                    // 손절: 현재 가치가 수취 프리미엄의 (1+stop_loss) 이상
                    else if (cur_val >= entry_premium_ * (1.0 + stop_loss_pct)) {
                        should_exit = true;
                        exit_reason = "손절";
                    }
                }
            }

            if (should_exit) {
                // 매수로 청산
                orders.push_back(Order::buy_call(call_strike_, expiry_, position_qty));
                orders.push_back(Order::buy_put(put_strike_,  expiry_, position_qty));
                position_ = 0;

                std::cerr << "[ShortStrangle] 청산 (" << exit_reason << "): CALL "
                          << call_strike_ << " PUT " << put_strike_ << "\n";
            }
        }

        return orders;
    }

    // ── 체결 콜백 ─────────────────────────────────────────────────
    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::OPTION) continue;
            std::cerr << "[ShortStrangle] 체결: "
                      << (o.side == Side::LONG ? "BUY " : "SELL ")
                      << (o.option_type == 'C' ? "CALL " : "PUT ")
                      << o.strike << " " << o.expiry
                      << " x" << o.quantity << "\n";
        }
    }

private:
    // ── 내부 상태 ─────────────────────────────────────────────────
    int         position_      = 0;    // 0=없음, -1=숏 스트랭글
    double      entry_premium_ = 0.0;  // 수취한 총 프리미엄 (USDT)
    int         call_strike_   = 0;
    int         put_strike_    = 0;
    std::string expiry_;

    // 잔존기간 (연 환산) — Binance 옵션 만기: 08:00 UTC
    double time_to_expiry(int64_t ts_ms, const std::string& expiry_str) const {
        int year, month, day;
        if (sscanf(expiry_str.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
            return 0.0;

        struct tm exp_tm = {};
        exp_tm.tm_year = year - 1900;
        exp_tm.tm_mon  = month - 1;
        exp_tm.tm_mday = day;
        exp_tm.tm_hour = 8;

        time_t exp_t = timegm(&exp_tm);
        double diff_sec = static_cast<double>(exp_t) - ts_ms / 1000.0;
        return std::max(0.0, diff_sec / (365.0 * 24.0 * 3600.0));
    }
};
