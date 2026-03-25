#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include "../include/bar_matrix.hpp"
#include <iostream>



class GoldenCrossStrategy : public Strategy {
public:
    int    fast_period       = 5;      
    int    slow_period       = 20;     
    double notional_per_trade = 100.0; 

    std::string name() const override { return "GoldenCross"; }

    void on_start() override {
        positions_.clear();
        prev_fast_.clear();
        prev_slow_.clear();

        std::cerr << "[GoldenCross] 시작 (멀티심볼)\n"
                  << "  fast_period        = " << fast_period        << "\n"
                  << "  slow_period        = " << slow_period        << "\n"
                  << "  notional_per_trade = $" << notional_per_trade << " / 심볼\n"
                  << "  워밍업             : " << slow_period        << "바\n";
    }

    //on bar start
    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {

        close_m_.update(snap);
        if (!close_m_.ready(slow_period)) return {};
        Column fast_ma = mean_column(close_m_, fast_period);
        Column slow_ma = mean_column(close_m_, slow_period);

        std::vector<Order> orders;

        Column cur_price = get_column(close_m_, 0);

        for (const auto& [sym, fast] : fast_ma) {
            const double slow  = slow_ma[sym];
            const double pf    = prev_fast_[sym]; 
            const double ps    = prev_slow_[sym];
            const double price = cur_price[sym];

            if (price <= 0) continue;  

            const double qty = notional_per_trade / price;

            const bool is_first     = (pf == 0.0);
            const bool golden_cross = !is_first && (pf <= ps) && (fast >  slow);
            const bool death_cross  = !is_first && (pf >= ps) && (fast <  slow);

            int& pos = positions_[sym];

            if (death_cross && pos <= 0) {
                if (pos == 1)
                    orders.push_back(Order::long_future(qty, sym));   // 숏 청산
                orders.push_back(Order::long_future(qty, sym));       // 롱 진입
                pos = -1;

            } else if (golden_cross && pos >= 0) {
                if (pos == -1)
                    orders.push_back(Order::short_future(qty, sym));  // 롱 청산
                orders.push_back(Order::short_future(qty, sym));      // 숏 진입
                pos = 1;
            }

            prev_fast_[sym] = fast;
            prev_slow_[sym] = slow;
        }

        return orders;
    }

    void on_fill(const std::vector<Order>& /*filled*/) override {
        //If you want to cout log, use this member function
    }

private:
    // ── BarMatrix ─────────────────────────────────────────────────
    BarMatrix close_m_ { Field::Close, 500 };

    // ── 심볼별 상태 ───────────────────────────────────────────────
    std::unordered_map<std::string, int>    positions_;
    std::unordered_map<std::string, double> prev_fast_;
    std::unordered_map<std::string, double> prev_slow_;

    // Moving Average helper function
    static Column mean_column(const BarMatrix& m, int period) {
        Column sum;
        int count = 0;
        for (int i = 0; i < period; ++i) {
            Column col = get_column(m, i);
            if (col.empty()) break;
            for (const auto& [sym, val] : col)
                sum.data[sym] += val;
            ++count;
        }
        if (count == 0) return Column{};
        return sum / static_cast<double>(count);
    }
};
