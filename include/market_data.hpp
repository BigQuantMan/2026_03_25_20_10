#pragma once
#include "types.hpp"
#include <algorithm>
#include <cmath>
#include <cstdint>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

class DataFeed;
class FuturesFeed;
class UnifiedFeed;
class BinanceRestPollingFuturesFeed;

class MarketSnapshot {
public:
    int64_t timestamp() const { return ts_; }
    double spot_price() const { return spot_price_; }

    const std::vector<OptionBar>& all_options() const { return options_; }
    bool empty() const { return options_.empty() && futures_.empty(); }

    std::optional<OptionBar> get(int strike, char type, const std::string& expiry) const {
        for (const auto& o : options_) {
            if (o.strike == strike && o.type == type && o.expiry == expiry) {
                return o;
            }
        }
        return std::nullopt;
    }

    std::vector<OptionBar> calls() const {
        return filter([](const OptionBar& o) { return o.type == 'C'; });
    }

    std::vector<OptionBar> puts() const {
        return filter([](const OptionBar& o) { return o.type == 'P'; });
    }

    std::vector<OptionBar> by_expiry(const std::string& expiry) const {
        return filter([&](const OptionBar& o) { return o.expiry == expiry; });
    }

    std::vector<std::string> available_expiries() const {
        std::vector<std::string> expiries;
        for (const auto& o : options_) {
            if (std::find(expiries.begin(), expiries.end(), o.expiry) == expiries.end()) {
                expiries.push_back(o.expiry);
            }
        }
        std::sort(expiries.begin(), expiries.end());
        return expiries;
    }

    std::string front_expiry() const {
        auto exps = available_expiries();
        return exps.empty() ? std::string() : exps.front();
    }

    std::optional<int> atm_strike(const std::string& expiry) const {
        auto opts = by_expiry(expiry);
        if (opts.empty()) {
            return std::nullopt;
        }

        int best = opts[0].strike;
        double best_dist = std::abs(opts[0].index_price - static_cast<double>(best));

        for (const auto& o : opts) {
            double dist = std::abs(o.index_price - static_cast<double>(o.strike));
            if (dist < best_dist) {
                best_dist = dist;
                best = o.strike;
            }
        }
        return best;
    }

    const FuturesBar* futures(const std::string& symbol) const {
        auto it = futures_.find(symbol);
        return it != futures_.end() ? &it->second : nullptr;
    }

    const std::unordered_map<std::string, FuturesBar>& all_futures() const {
        return futures_;
    }

    std::unordered_map<std::string, FuturesBar> active_futures() const {
        std::unordered_map<std::string, FuturesBar> result;
        result.reserve(futures_.size());
        for (const auto& kv : futures_) {
            if (kv.second.is_active) {
                result.emplace(kv.first, kv.second);
            }
        }
        return result;
    }

    std::vector<std::string> futures_symbols() const {
        std::vector<std::string> syms;
        syms.reserve(futures_.size());
        for (const auto& kv : futures_) {
            syms.push_back(kv.first);
        }
        return syms;
    }

    static MarketSnapshot make_futures_snapshot(
        int64_t ts,
        double spot,
        std::unordered_map<std::string, FuturesBar> futures)
    {
        return MarketSnapshot(ts, spot, std::move(futures));
    }

private:
    friend class DataFeed;
    friend class FuturesFeed;
    friend class UnifiedFeed;
    friend class BinanceRestPollingFuturesFeed;

    int64_t ts_ = 0;
    double spot_price_ = 0.0;
    std::vector<OptionBar> options_;
    std::unordered_map<std::string, FuturesBar> futures_;

    MarketSnapshot() = default;

    MarketSnapshot(int64_t ts, double spot, std::vector<OptionBar> opts)
        : ts_(ts), spot_price_(spot), options_(std::move(opts)) {}

    MarketSnapshot(int64_t ts, double spot, std::unordered_map<std::string, FuturesBar> futures)
        : ts_(ts), spot_price_(spot), futures_(std::move(futures)) {}

    MarketSnapshot(
        int64_t ts,
        double spot,
        std::vector<OptionBar> opts,
        std::unordered_map<std::string, FuturesBar> futures)
        : ts_(ts), spot_price_(spot), options_(std::move(opts)), futures_(std::move(futures)) {}

    template <typename Pred>
    std::vector<OptionBar> filter(Pred pred) const {
        std::vector<OptionBar> result;
        for (const auto& o : options_) {
            if (pred(o)) {
                result.push_back(o);
            }
        }
        return result;
    }
};
