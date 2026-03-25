#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include "../include/bar_matrix.hpp"
#include <iostream>
#include <unordered_map>

class LiveFundingMomentumStrategy : public Strategy {
public:
    double      long_threshold  = -0.0001;
    double      short_threshold = +0.0001;
    double      exit_threshold  =  0.00005;
    double      quantity        =  0.01;
    std::string symbol          = "BTCUSDT";

    std::string name() const override { return "LiveFundingMomentum"; }

    void on_start() override {
        position_ = 0;
        std::cerr << "[LiveFundingMomentum] start\n"
                  << "  symbol          = " << symbol << "\n"
                  << "  long_threshold  = " << long_threshold << "\n"
                  << "  short_threshold = " << short_threshold << "\n"
                  << "  exit_threshold  = " << exit_threshold << "\n";
    }

    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {
        const FuturesBar* bar = snap.futures(symbol);
        if (!bar) return {};

        const double fr = bar->funding_rate;
        std::vector<Order> orders;

        if (position_ == 0) {
            if (fr <= long_threshold) {
                orders.push_back(Order::long_future(quantity, symbol));
            } else if (fr >= short_threshold) {
                orders.push_back(Order::short_future(quantity, symbol));
            }
        } else if (position_ == 1) {
            if (fr > -exit_threshold) {
                orders.push_back(Order::short_future(quantity, symbol));
            }
        } else if (position_ == -1) {
            if (fr < exit_threshold) {
                orders.push_back(Order::long_future(quantity, symbol));
            }
        }
        return orders;
    }

    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::FUTURE || o.symbol != symbol) continue;
            if (o.side == Side::LONG) {
                if (position_ == -1) position_ = 0;
                else                 position_ = 1;
            } else {
                if (position_ == 1)  position_ = 0;
                else                 position_ = -1;
            }
        }
    }

private:
    int position_ = 0;
};

class LiveGoldenCrossStrategy : public Strategy {
public:
    int    fast_period = 5;
    int    slow_period = 20;
    double notional_per_trade = 100.0;

    std::string name() const override { return "LiveGoldenCross"; }

    void on_start() override {
        close_m_ = BarMatrix(Field::Close, 500);
        prev_fast_.clear();
        prev_slow_.clear();
        state_.clear();
        std::cerr << "[LiveGoldenCross] start\n"
                  << "  fast_period        = " << fast_period << "\n"
                  << "  slow_period        = " << slow_period << "\n"
                  << "  notional_per_trade = $" << notional_per_trade << "\n";
    }

    std::vector<Order> on_bar(int64_t /*ts*/, const MarketSnapshot& snap) override {
        close_m_.update(snap);
        if (!close_m_.ready(slow_period)) return {};

        Column fast_ma  = mean_column(close_m_, fast_period);
        Column slow_ma  = mean_column(close_m_, slow_period);
        Column cur_price = get_column(close_m_, 0);

        std::vector<Order> orders;

        for (const auto& [sym, fast] : fast_ma) {
            auto slow_it = slow_ma.data.find(sym);
            auto px_it   = cur_price.data.find(sym);
            if (slow_it == slow_ma.end() || px_it == cur_price.end()) continue;

            const double slow  = slow_it->second;
            const double price = px_it->second;
            if (price <= 0.0) continue;

            const double prev_fast = prev_fast_[sym];
            const double prev_slow = prev_slow_[sym];
            const bool   first_bar = (prev_fast == 0.0 && prev_slow == 0.0);

            const bool golden_cross = !first_bar && (prev_fast <= prev_slow) && (fast > slow);
            const bool death_cross  = !first_bar && (prev_fast >= prev_slow) && (fast < slow);
            const double qty = notional_per_trade / price;
            int cur_state = state_[sym];

            if (golden_cross && cur_state <= 0) {
                if (cur_state == -1) orders.push_back(Order::long_future(qty, sym));
                orders.push_back(Order::long_future(qty, sym));
            } else if (death_cross && cur_state >= 0) {
                if (cur_state == 1)  orders.push_back(Order::short_future(qty, sym));
                orders.push_back(Order::short_future(qty, sym));
            }

            prev_fast_[sym] = fast;
            prev_slow_[sym] = slow;
        }

        return orders;
    }

    void on_fill(const std::vector<Order>& filled) override {
        for (const auto& o : filled) {
            if (o.instrument != Instrument::FUTURE) continue;
            int& st = state_[o.symbol];
            if (o.side == Side::LONG) {
                if (st == -1) st = 0;
                else          st = 1;
            } else {
                if (st == 1)  st = 0;
                else          st = -1;
            }
        }
    }

private:
    BarMatrix close_m_{Field::Close, 500};
    std::unordered_map<std::string, double> prev_fast_;
    std::unordered_map<std::string, double> prev_slow_;
    std::unordered_map<std::string, int>    state_;

    static Column mean_column(const BarMatrix& m, int period) {
        Column sum;
        int count = 0;
        for (int i = 0; i < period; ++i) {
            Column col = get_column(m, i);
            if (col.empty()) break;
            for (const auto& [sym, v] : col.data) sum.data[sym] += v;
            ++count;
        }
        if (count == 0) return {};
        for (auto& [sym, v] : sum.data) v /= static_cast<double>(count);
        return sum;
    }
};
