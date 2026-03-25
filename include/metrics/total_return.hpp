#pragma once
#include "metric_base.hpp"

// 전체 기간 누적 수익률 (%)
class TotalReturn : public Metric {
public:
    void on_bar(int64_t ts, const PortfolioState& state) override;
    double      result() const override;
    std::string name()   const override { return "total_return_pct"; }

private:
    double initial_value_ = 0.0;
    double final_value_   = 0.0;
    bool   initialized_   = false;
};
