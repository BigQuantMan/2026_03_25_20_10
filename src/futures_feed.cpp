#include "../include/futures_feed.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <map>

// ─────────────────────────────────────────────────────────────────
//  CSV 컬럼 순서 (prepare_futures_data.py 출력과 반드시 일치):
//  timestamp, symbol,
//  open, high, low, close, volume, quote_volume,
//  funding_rate, open_interest, long_ratio, short_ratio, taker_buy_ratio,
//  is_active   (optional, col[13]: 1=top-N 유니버스, 0=유니버스 외)
// ─────────────────────────────────────────────────────────────────

static std::string ft_trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> ft_split_csv(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ','))
        tokens.push_back(ft_trim(token));
    return tokens;
}

static double ft_safe_double(const std::string& s) {
    if (s.empty() || s == "nan" || s == "NaN" || s == "NA") return 0.0;
    try { return std::stod(s); } catch (...) { return 0.0; }
}

FuturesFeed::FuturesFeed(const std::string& csv_path,
                         const std::string& spot_symbol)
    : spot_symbol_(spot_symbol)
{
    load_csv(csv_path);
}

void FuturesFeed::load_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("FuturesFeed: cannot open " + path);

    // timestamp → unordered_map<symbol, FuturesBar> 임시 맵
    std::map<int64_t, std::unordered_map<std::string, FuturesBar>> ts_map;

    std::string line;
    std::getline(f, line);  // 헤더 스킵

    size_t row = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tok = ft_split_csv(line);
        if (tok.size() < 13) continue;

        FuturesBar bar;
        bar.timestamp       = std::stoll(tok[0]);
        bar.symbol          = tok[1];
        bar.open            = ft_safe_double(tok[2]);
        bar.high            = ft_safe_double(tok[3]);
        bar.low             = ft_safe_double(tok[4]);
        bar.close           = ft_safe_double(tok[5]);
        bar.volume          = ft_safe_double(tok[6]);
        bar.quote_volume    = ft_safe_double(tok[7]);
        bar.funding_rate    = ft_safe_double(tok[8]);
        bar.open_interest   = ft_safe_double(tok[9]);
        bar.long_ratio      = ft_safe_double(tok[10]);
        bar.short_ratio     = ft_safe_double(tok[11]);
        bar.taker_buy_ratio = ft_safe_double(tok[12]);
        // is_active: col[13] optional (없으면 true, "0" 이면 false)
        bar.is_active = (tok.size() < 14 || tok[13] != "0");

        // close = 0인 행 제외 (데이터 없음)
        if (bar.close <= 0.0) continue;

        ts_map[bar.timestamp][bar.symbol] = bar;
        ++row;
    }

    if (ts_map.empty())
        throw std::runtime_error("FuturesFeed: no valid data in " + path);

    // 타임스탬프별 MarketSnapshot 생성 (시간 오름차순 보장)
    snapshots_.reserve(ts_map.size());
    for (auto& [ts, bars] : ts_map) {
        // spot_price = spot_symbol_ 의 close (없으면 0)
        double spot = 0.0;
        auto it = bars.find(spot_symbol_);
        if (it != bars.end()) spot = it->second.close;

        // FuturesFeed의 friend 생성자 호출
        snapshots_.emplace_back(MarketSnapshot(ts, spot, std::move(bars)));
    }

    std::cerr << "[FuturesFeed] Loaded " << row << " rows → "
              << snapshots_.size() << " timestamps from " << path << "\n";
}

bool FuturesFeed::next() {
    if (cursor_ == SIZE_MAX) {
        cursor_ = 0;
    } else {
        ++cursor_;
    }
    return cursor_ < snapshots_.size();
}

const MarketSnapshot& FuturesFeed::current() const {
    if (cursor_ == SIZE_MAX || cursor_ >= snapshots_.size())
        throw std::runtime_error("FuturesFeed: current() called before next() or after end");
    return snapshots_[cursor_];
}

size_t FuturesFeed::remaining() const {
    if (cursor_ == SIZE_MAX) return snapshots_.size();
    if (cursor_ >= snapshots_.size()) return 0;
    return snapshots_.size() - cursor_ - 1;
}

int64_t FuturesFeed::start_ts() const {
    return snapshots_.empty() ? 0 : snapshots_.front().timestamp();
}

int64_t FuturesFeed::end_ts() const {
    return snapshots_.empty() ? 0 : snapshots_.back().timestamp();
}

void FuturesFeed::reset() {
    cursor_ = SIZE_MAX;
}
