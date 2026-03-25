#pragma once
#include "metric_base.hpp"
#include <vector>
#include <cmath>

// 일별 수익률 기준 연간화 Sharpe Ratio (연간화 계수 = sqrt(365))
class SharpeRatio : public Metric {
public:
    void on_bar(int64_t ts, const PortfolioState& state) override;
    double      result() const override;
    std::string name()   const override { return "sharpe"; }

private:
    double   prev_value_    = 0.0;
    int64_t  prev_ts_       = 0;
    int64_t  last_day_ts_   = 0;
    double   day_start_val_ = 0.0;

    std::vector<double> daily_returns_;

    bool initialized_ = false;
};
