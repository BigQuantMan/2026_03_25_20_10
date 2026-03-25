#pragma once
#include "metric_base.hpp"
#include <vector>

// 일별 평균 Turnover = 일별 거래 대금 / 포트폴리오 가치
class DailyTurnover : public Metric {
public:
    void on_bar(int64_t ts, const PortfolioState& state) override;
    double      result() const override;
    std::string name()   const override { return "avg_daily_turnover"; }

private:
    std::vector<double> daily_turnovers_;
    int64_t             last_day_ts_ = 0;
    double              day_traded_  = 0.0;
    double              day_value_   = 0.0;
};
