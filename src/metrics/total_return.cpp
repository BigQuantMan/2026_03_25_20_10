#include "../../include/metrics/total_return.hpp"

void TotalReturn::on_bar(int64_t /*ts*/, const PortfolioState& state) {
    if (!initialized_) {
        initial_value_ = state.total_value;
        initialized_   = true;
    }
    final_value_ = state.total_value;
}

double TotalReturn::result() const {
    if (!initialized_ || initial_value_ <= 0) return 0.0;
    return (final_value_ - initial_value_) / initial_value_ * 100.0;
}

REGISTER_METRIC("total_return_pct", TotalReturn)
