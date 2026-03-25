#pragma once
#include "../include/strategy.hpp"
#include <unordered_map>
#include <string>
#include <cmath>
#include <optional>

// ─────────────────────────────────────────────────────────────────
//  Put-Call Parity 차익거래 예시 전략
//
//  전략 로직:
//    ε = (C − P) − (S − K·e^{−rT})
//
//    ε > threshold → Call 고평가: Sell Call + Buy Put + Long Spot
//    ε < -threshold → Put 고평가: Buy Call + Sell Put + Short Future
//    |ε| < exit_threshold → 청산
//
//  전략 제작자 참고용 최소 예시:
//    - on_bar() 안에서 snap.* 조회만 허용
//    - 미래 데이터 접근 불가 (DataFeed 없음)
//    - 과거 데이터는 멤버 변수에 직접 저장하여 사용
// ─────────────────────────────────────────────────────────────────

class PutCallParityStrategy : public Strategy {
public:
    // ── 파라미터 ──────────────────────────────────────────────
    double risk_free_rate = 0.05;    // 연 무위험이자율
    double fee_rate       = 0.0003;  // 거래소 수수료 0.03%/leg  (노트북 FEE_RATE)
    double spread_est     = 0.002;   // bid-ask 스프레드 추정 0.2% (노트북 SPREAD_EST)
    double entry_mult     = 3.0;     // cost × N배 초과 시 진입   (노트북 ENTRY_MULT)
    double exit_mult      = 0.5;     // cost × N배 이하 시 청산   (노트북 EXIT_MULT)
    double position_qty   = 0.001;   // 포지션 크기 (BTC)
    std::string target_expiry = "";  // "" = front month 자동 선택

    std::string name() const override { return "PutCallParity"; }

    // ─────────────────────────────────────────────────────────
    // on_fill: 실제 체결된 주문 목록을 받아 포지션 상태 정확히 업데이트
    // ─────────────────────────────────────────────────────────
    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::OPTION) continue;

            std::string pos_key = o.expiry + "_" + std::to_string(o.strike);

            // 진입 주문 체결 확인 (pending → confirmed)
            auto pit = pending_entry_.find(pos_key);
            if (pit != pending_entry_.end()) {
                // pending에 있던 진입 주문이 체결됨 → confirmed로 이동
                confirmed_dir_[pos_key] = pit->second;
                pending_entry_.erase(pit);
                continue;
            }

            // 청산 주문 체결 확인
            auto cit = confirmed_dir_.find(pos_key);
            if (cit != confirmed_dir_.end()) {
                // 청산 방향 주문이 체결됨 → 포지션 제거
                int dir = cit->second;
                bool is_exit = (dir == +1 && o.side == Side::LONG)   // buy_call = 청산
                            || (dir == -1 && o.side == Side::SHORT);  // sell_call = 청산
                if (is_exit) {
                    confirmed_dir_.erase(cit);
                }
            }
        }
    }

    // ─────────────────────────────────────────────────────────
    std::vector<Order> on_bar(int64_t ts, const MarketSnapshot& snap) override {
        std::vector<Order> orders;

        // 만기 결정
        std::string expiry = target_expiry.empty()
                             ? snap.front_expiry()
                             : target_expiry;
        if (expiry.empty()) return orders;

        // ATM 행사가 조회
        auto atm = snap.atm_strike(expiry);
        if (!atm) return orders;
        int K = *atm;

        // ── Real 데이터 + 연속성(stability) 확인 ────────────
        // t bar에만 데이터 있어도 t+1에 없을 수 있음 (확률 ~13% 실패).
        // t-1과 t 모두 real 데이터 있는 경우 t+1에도 있을 확률이 훨씬 높음.
        // → 연속 2 bar 이상 실제 데이터가 있는 옵션만 진입 허용.
        bool call_real   = snap.get(K, 'C', expiry).has_value();
        bool put_real    = snap.get(K, 'P', expiry).has_value();

        // 이전 bar에서도 real 데이터가 있었는지 (cache 확인)
        // get_with_ffill 호출 전에 cache를 먼저 읽어야 함
        static constexpr int64_t BAR_MS = 5LL * 60 * 1000;  // 5분
        auto call_cache_it = price_cache_.find(cache_key(K, 'C', expiry));
        auto put_cache_it  = price_cache_.find(cache_key(K, 'P', expiry));
        bool call_stable = call_real &&
                           call_cache_it != price_cache_.end() &&
                           (ts - call_cache_it->second.ts) <= BAR_MS;
        bool put_stable  = put_real &&
                           put_cache_it != price_cache_.end() &&
                           (ts - put_cache_it->second.ts) <= BAR_MS;

        // 패리티 오차 계산은 ffill 데이터로 (신호 판단 목적)
        auto call_opt = get_with_ffill(snap, K, 'C', expiry, ts);
        auto put_opt  = get_with_ffill(snap, K, 'P', expiry, ts);
        if (!call_opt || !put_opt) return orders;

        double C = call_opt->mark_price;
        double P = put_opt->mark_price;
        double S = snap.spot_price();
        if (S <= 0 || C <= 0 || P <= 0) return orders;

        // 잔존기간 (연 환산)
        double T = time_to_expiry(ts, expiry);
        if (T <= 0) return orders;

        // 패리티 오차
        double rhs = S - K * std::exp(-risk_free_rate * T);
        double err = (C - P) - rhs;

        // ── 동적 거래 비용 계산 (노트북 동일 공식) ───────────
        // cost = (C+P) × (fee + spread/2) × 2legs + S × fee × 2sides
        double cost         = (C + P) * (fee_rate + spread_est / 2.0) * 2.0
                            + S * fee_rate * 2.0;
        double entry_thresh = cost * entry_mult;
        double exit_thresh  = cost * exit_mult;

        std::string pos_key = expiry + "_" + std::to_string(K);

        bool has_confirmed = confirmed_dir_.count(pos_key) > 0;
        bool has_pending   = pending_entry_.count(pos_key) > 0;

        if (!has_confirmed && !has_pending) {
            // ── 포지션 없음: 진입 조건 확인 ─────────────────
            // 연속 2 bar 모두 real 데이터 있어야 진입 (partial fill 방지)
            // t-1, t 모두 있으면 t+1(실제 체결 시점)에도 있을 확률 높음
            if (!call_stable || !put_stable) {
                // 연속성 미충족 → 진입 포기
            } else if (err > entry_thresh) {
                // Call 고평가: Sell Call + Buy Put + Long Spot
                orders.push_back(Order::sell_call(K, expiry, position_qty));
                orders.push_back(Order::buy_put(K, expiry, position_qty));
                orders.push_back(Order::buy_spot(position_qty));
                pending_entry_[pos_key] = +1;  // on_fill() 대기

            } else if (err < -entry_thresh) {
                // Put 고평가: Buy Call + Sell Put + Short Future
                orders.push_back(Order::buy_call(K, expiry, position_qty));
                orders.push_back(Order::sell_put(K, expiry, position_qty));
                orders.push_back(Order::short_future(position_qty));
                pending_entry_[pos_key] = -1;  // on_fill() 대기
            }

        } else if (has_confirmed) {
            // ── 포지션 있음(체결 확인됨): 청산 조건 확인 ────
            // 청산도 연속 2 bar real 데이터 있을 때만 (partial 청산 방지)
            if (call_stable && put_stable && std::abs(err) < exit_thresh) {
                int dir = confirmed_dir_[pos_key];
                if (dir == +1) {
                    orders.push_back(Order::buy_call(K, expiry, position_qty));
                    orders.push_back(Order::sell_put(K, expiry, position_qty));
                    orders.push_back(Order::sell_spot(position_qty));
                } else {
                    orders.push_back(Order::sell_call(K, expiry, position_qty));
                    orders.push_back(Order::buy_put(K, expiry, position_qty));
                    orders.push_back(Order::long_future(position_qty));
                }
                // 청산 주문 제출 (on_fill()에서 confirmed_dir_ 제거)
            }

        } else {
            // ── pending 상태: 이전 진입 주문이 아직 체결 안 됨 ──
            // 다음 바에서 on_fill() 호출 대기
            // fill price = 0이 지속되면 pending이 쌓이는 것을 방지
            // → 진입 제출 후 max_pending_bars 이상 경과 시 포기
            auto age_it = pending_age_.find(pos_key);
            if (age_it == pending_age_.end()) {
                pending_age_[pos_key] = ts;
            } else if (ts - age_it->second > max_pending_ms) {
                // 너무 오래됨 → 포기하고 pending 취소
                pending_entry_.erase(pos_key);
                pending_age_.erase(pos_key);
            }
        }

        return orders;
    }

private:
    // ── 포지션 추적 ───────────────────────────────────────────
    std::unordered_map<std::string, int>     pending_entry_;
    std::unordered_map<std::string, int>     confirmed_dir_;
    std::unordered_map<std::string, int64_t> pending_age_;
    static constexpr int64_t max_pending_ms = 30LL * 60 * 1000;

    // ── 옵션 가격 캐시 (전략 내부 forward-fill) ───────────────
    // 데이터 공백 시 최대 max_stale_ms 동안 마지막 알려진 가격 사용
    struct CachedBar {
        int64_t  ts;   // 마지막으로 실제 데이터가 있던 시각 (ms)
        OptionBar bar; // 마지막으로 본 OptionBar (값 복사)
    };
    std::unordered_map<std::string, CachedBar> price_cache_;

    // 최대 stale 허용 시간: 15분 (5분 bar 기준 3개)
    static constexpr int64_t max_stale_ms = 15LL * 60 * 1000;

    // cache_key 생성: "expiry_strike_type"
    static std::string cache_key(int strike, char type, const std::string& expiry) {
        return expiry + "_" + std::to_string(strike) + "_" + type;
    }

    // snap에서 조회 → 없으면 캐시에서 forward-fill
    std::optional<OptionBar> get_with_ffill(
        const MarketSnapshot& snap, int K, char type,
        const std::string& expiry, int64_t ts)
    {
        std::string key = cache_key(K, type, expiry);
        auto opt = snap.get(K, type, expiry);

        if (opt.has_value()) {
            // 실제 데이터 → 캐시 갱신
            price_cache_[key] = { ts, *opt };
            return opt;
        }

        // 데이터 없음 → 캐시에서 forward-fill
        auto it = price_cache_.find(key);
        if (it != price_cache_.end() && ts - it->second.ts <= max_stale_ms) {
            return it->second.bar;  // stale 허용 범위 내 → 캐시 반환
        }

        return std::nullopt;  // 캐시도 너무 오래됨 → 진입 안 함
    }

    // 잔존기간 (연 환산) 계산
    double time_to_expiry(int64_t ts_ms, const std::string& expiry_str) const {
        // expiry_str: "2026-01-25" → 만기 08:00 UTC
        int year, month, day;
        if (sscanf(expiry_str.c_str(), "%d-%d-%d", &year, &month, &day) != 3)
            return 0.0;

        struct tm exp_tm = {};
        exp_tm.tm_year = year - 1900;
        exp_tm.tm_mon  = month - 1;
        exp_tm.tm_mday = day;
        exp_tm.tm_hour = 8;   // Binance 옵션 만기: 08:00 UTC

        time_t exp_t = timegm(&exp_tm);
        double diff_sec = static_cast<double>(exp_t) - ts_ms / 1000.0;
        return std::max(0.0, diff_sec / (365.0 * 24 * 3600));
    }
};
