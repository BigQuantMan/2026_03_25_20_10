#include "../../include/metrics/sharpe.hpp"
#include <cmath>
#include <numeric>

// 86400000 ms = 1일
static constexpr int64_t ONE_DAY_MS = 86400000LL;

void SharpeRatio::on_bar(int64_t ts, const PortfolioState& state) {
    if (!initialized_) {
        day_start_val_ = state.total_value;
        last_day_ts_   = ts - (ts % ONE_DAY_MS);  // 오늘 자정 UTC
        initialized_   = true;
        return;
    }

    // 하루가 넘어가면 일별 수익률 기록
    int64_t cur_day = ts - (ts % ONE_DAY_MS);
    if (cur_day > last_day_ts_) {
        if (day_start_val_ > 0) {
            double ret = (state.total_value - day_start_val_) / day_start_val_;
            daily_returns_.push_back(ret);
        }
        day_start_val_ = state.total_value;
        last_day_ts_   = cur_day;
    }
}

double SharpeRatio::result() const {
    if (daily_returns_.size() < 2) return 0.0;

    double mean = std::accumulate(daily_returns_.begin(), daily_returns_.end(), 0.0)
                  / daily_returns_.size();
    double sq_sum = 0.0;
    for (double r : daily_returns_)
        sq_sum += (r - mean) * (r - mean);
    double std_dev = std::sqrt(sq_sum / (daily_returns_.size() - 1));

    if (std_dev < 1e-10) return 0.0;

    // 연간화: sqrt(365) (무위험이자율 0% 가정)
    return (mean / std_dev) * std::sqrt(365.0);
}

REGISTER_METRIC("sharpe", SharpeRatio)
