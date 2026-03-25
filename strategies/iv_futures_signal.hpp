#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include <string>
#include <deque>
#include <cmath>
#include <iostream>

// ─────────────────────────────────────────────────────────────────
//  IVFuturesSignalStrategy  (옵션 IV 스큐 → 선물 방향 매매)
//  ──────────────────────────────────────────────────────────────
//  아이디어:
//    - ATM 풋 IV와 ATM 콜 IV의 차이(skew)로 시장 심리를 측정
//    - 스큐 = put_iv − call_iv
//
//      put_iv > call_iv  (skew > 0) : 시장 참가자들이 하락을 방어 중
//                                     → 비관적 심리 → SHORT 선물
//
//      call_iv > put_iv  (skew < 0) : 시장 참가자들이 상승에 베팅 중
//                                     → 낙관적 심리 → LONG 선물
//
//  신호 스무딩:
//    스큐 값을 최근 skew_smooth_bars 개의 평균으로 스무딩하여
//    노이즈를 줄임
//
//  파라미터:
//    skew_entry_threshold : 이 값 초과 시 진입 (기본: 0.05 = 5%)
//    skew_exit_threshold  : 이 값 이하로 회귀 시 청산 (기본: 0.02 = 2%)
//    skew_smooth_bars     : 스큐 스무딩 바 수 (기본: 3)
//    quantity             : 선물 거래 수량 (기본: 0.01 BTC)
//    symbol               : 선물 심볼 (기본: "BTCUSDT")
//    target_expiry        : "" = front month 자동 선택
//
//  ※ 데이터: 옵션 데이터 필수 (IV 신호)
//             선물 데이터 선택 (있으면 실제 선물 가격으로 체결;
//                               없으면 spot_price() 로 fallback)
//
//  ※ 최소 skew_smooth_bars 개 바 이후부터 신호 발생
// ─────────────────────────────────────────────────────────────────

class IVFuturesSignalStrategy : public Strategy {
public:
    // ── 파라미터 ──────────────────────────────────────────────────
    double      skew_entry_threshold = 0.05;  // 진입 스큐 임계치 (5%)
    double      skew_exit_threshold  = 0.02;  // 청산 스큐 임계치 (2%)
    int         skew_smooth_bars     = 3;     // 스큐 스무딩 바 수
    double      quantity             = 0.01;  // 선물 수량 (BTC)
    std::string symbol               = "BTCUSDT";
    std::string target_expiry        = "";    // "" = front month

    std::string name() const override { return "IVFuturesSignal"; }

    // ── 초기화 ────────────────────────────────────────────────────
    void on_start() override {
        position_ = 0;
        skew_buf_.clear();

        std::cerr << "[IVFuturesSignal] 시작\n"
                  << "  skew_entry_threshold = " << (skew_entry_threshold * 100) << "%\n"
                  << "  skew_exit_threshold  = " << (skew_exit_threshold  * 100) << "%\n"
                  << "  skew_smooth_bars     = " << skew_smooth_bars << "\n"
                  << "  quantity             = " << quantity << " BTC\n"
                  << "  symbol               = " << symbol  << "\n"
                  << "  신호: put_iv − call_iv > +" << (skew_entry_threshold*100)
                     << "% → SHORT,  < −" << (skew_entry_threshold*100) << "% → LONG\n";
    }

    // ── 매 바 처리 ────────────────────────────────────────────────
    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {
        std::vector<Order> orders;

        // 만기 결정
        const std::string expiry = target_expiry.empty()
                                   ? snap.front_expiry()
                                   : target_expiry;
        if (expiry.empty()) return orders;

        // ATM 행사가
        auto atm_k = snap.atm_strike(expiry);
        if (!atm_k) return orders;
        const int K = *atm_k;

        // ATM 콜 / 풋 IV 조회
        auto call_opt = snap.get(K, 'C', expiry);
        auto put_opt  = snap.get(K, 'P', expiry);
        if (!call_opt || !put_opt) return orders;

        const double call_iv = call_opt->mark_iv;
        const double put_iv  = put_opt->mark_iv;
        if (call_iv <= 0 || put_iv <= 0) return orders;

        // ── 스큐 계산 및 스무딩 ───────────────────────────────────
        // skew > 0 : 풋 비쌈 → 시장 비관적 (bearish)
        // skew < 0 : 콜 비쌈 → 시장 낙관적 (bullish)
        const double skew = put_iv - call_iv;

        skew_buf_.push_back(skew);
        if ((int)skew_buf_.size() > skew_smooth_bars)
            skew_buf_.pop_front();

        // 워밍업: 충분한 바 쌓이기 전까지 대기
        if ((int)skew_buf_.size() < skew_smooth_bars) return orders;

        double avg_skew = 0.0;
        for (const double s : skew_buf_) avg_skew += s;
        avg_skew /= static_cast<double>(skew_buf_.size());

        // ── 청산 조건 ─────────────────────────────────────────────
        if (position_ == 1) {
            // LONG 포지션: skew가 양수(bearish)로 전환 시 청산
            if (avg_skew > skew_exit_threshold) {
                orders.push_back(Order::short_future(quantity, symbol));
                position_ = 0;
                std::cerr << "[IVFuturesSignal] LONG 청산: skew=" << avg_skew << "\n";
            }
        } else if (position_ == -1) {
            // SHORT 포지션: skew가 음수(bullish)로 전환 시 청산
            if (avg_skew < -skew_exit_threshold) {
                orders.push_back(Order::long_future(quantity, symbol));
                position_ = 0;
                std::cerr << "[IVFuturesSignal] SHORT 청산: skew=" << avg_skew << "\n";
            }
        }

        // ── 진입 조건 (포지션 없을 때만) ─────────────────────────
        if (position_ == 0) {
            if (avg_skew > skew_entry_threshold) {
                // 풋 skew 높음 → 하락 방어 수요 → SHORT 선물
                orders.push_back(Order::short_future(quantity, symbol));
                position_ = -1;
                std::cerr << "[IVFuturesSignal] SHORT 진입: skew=" << avg_skew
                          << " (" << symbol << " @" << snap.spot_price() << ")\n";

            } else if (avg_skew < -skew_entry_threshold) {
                // 콜 skew 높음 → 상승 기대 → LONG 선물
                orders.push_back(Order::long_future(quantity, symbol));
                position_ = 1;
                std::cerr << "[IVFuturesSignal] LONG 진입: skew=" << avg_skew
                          << " (" << symbol << " @" << snap.spot_price() << ")\n";
            }
        }

        return orders;
    }

    // ── 체결 콜백 ─────────────────────────────────────────────────
    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::FUTURE) continue;
            std::cerr << "[IVFuturesSignal] 체결: "
                      << (o.side == Side::LONG ? "LONG " : "SHORT ")
                      << o.quantity << " " << o.symbol
                      << "  (포지션: "
                      << (position_ ==  1 ? "롱" :
                          position_ == -1 ? "숏" : "없음")
                      << ")\n";
        }
    }

private:
    // ── 내부 상태 ─────────────────────────────────────────────────
    int                position_ = 0;    // 0=없음, 1=롱, -1=숏
    std::deque<double> skew_buf_;        // 최근 skew_smooth_bars 개의 스큐 값
};
