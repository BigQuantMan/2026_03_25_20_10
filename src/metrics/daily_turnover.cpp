#include "../../include/metrics/daily_turnover.hpp"
#include <numeric>

static constexpr int64_t ONE_DAY_MS = 86400000LL;

void DailyTurnover::on_bar(int64_t ts, const PortfolioState& state) {
    int64_t cur_day = ts - (ts % ONE_DAY_MS);

    if (last_day_ts_ == 0) {
        last_day_ts_ = cur_day;
    }

    if (cur_day > last_day_ts_) {
        // 하루 마감: turnover = 당일 거래 대금 / 포트폴리오 가치
        if (day_value_ > 0)
            daily_turnovers_.push_back(day_traded_ / day_value_);

        day_traded_  = 0.0;
        day_value_   = 0.0;
        last_day_ts_ = cur_day;
    }

    day_traded_ = state.daily_traded_notional;
    day_value_  = state.total_value;
}

double DailyTurnover::result() const {
    if (daily_turnovers_.empty()) return 0.0;
    return std::accumulate(daily_turnovers_.begin(), daily_turnovers_.end(), 0.0)
           / daily_turnovers_.size();
}

REGISTER_METRIC("avg_daily_turnover", DailyTurnover)
