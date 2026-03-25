#pragma once
#include "../include/strategy.hpp"
#include "../include/market_data.hpp"
#include <string>

// ─────────────────────────────────────────────────────────────────
//  gold_corss
// ─────────────────────────────────────────────────────────────────
class gold_corss : public Strategy {
public:
    std::string name() const override { return "gold_corss"; }

    void on_bar(const MarketSnapshot& snap, Portfolio& portfolio) override {
        // TODO: 전략 로직 구현
    }
};
