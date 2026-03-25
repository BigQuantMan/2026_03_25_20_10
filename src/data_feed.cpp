#include "../include/data_feed.hpp"
#include <fstream>
#include <sstream>
#include <stdexcept>
#include <algorithm>
#include <iostream>
#include <map>

// ─────────────────────────────────────────────────────────────────
//  CSV 컬럼 순서 (prepare_data.py 출력과 반드시 일치):
//  timestamp, symbol, strike, expiry, type,
//  mark_price, mark_iv, index_price,
//  delta, gamma, theta, vega,
//  best_bid, best_ask, open_interest
// ─────────────────────────────────────────────────────────────────

static std::string trim(const std::string& s) {
    size_t start = s.find_first_not_of(" \t\r\n");
    size_t end   = s.find_last_not_of(" \t\r\n");
    if (start == std::string::npos) return "";
    return s.substr(start, end - start + 1);
}

static std::vector<std::string> split_csv(const std::string& line) {
    std::vector<std::string> tokens;
    std::stringstream ss(line);
    std::string token;
    while (std::getline(ss, token, ','))
        tokens.push_back(trim(token));
    return tokens;
}

static double safe_double(const std::string& s) {
    if (s.empty() || s == "nan" || s == "NaN" || s == "NA") return 0.0;
    try { return std::stod(s); } catch (...) { return 0.0; }
}

DataFeed::DataFeed(const std::string& csv_path) {
    load_csv(csv_path);
}

void DataFeed::load_csv(const std::string& path) {
    std::ifstream f(path);
    if (!f.is_open())
        throw std::runtime_error("DataFeed: cannot open " + path);

    // timestamp → vector<OptionBar> 임시 맵
    std::map<int64_t, std::vector<OptionBar>> ts_map;

    std::string line;
    std::getline(f, line);  // 헤더 스킵

    size_t row = 0;
    while (std::getline(f, line)) {
        if (line.empty()) continue;
        auto tok = split_csv(line);
        if (tok.size() < 15) continue;

        OptionBar bar;
        bar.timestamp    = std::stoll(tok[0]);
        bar.symbol       = tok[1];
        bar.strike       = std::stoi(tok[2]);
        bar.expiry       = tok[3];
        bar.type         = tok[4].empty() ? '\0' : tok[4][0];
        bar.mark_price   = safe_double(tok[5]);
        bar.mark_iv      = safe_double(tok[6]);
        bar.index_price  = safe_double(tok[7]);
        bar.delta        = safe_double(tok[8]);
        bar.gamma        = safe_double(tok[9]);
        bar.theta        = safe_double(tok[10]);
        bar.vega         = safe_double(tok[11]);
        bar.best_bid     = safe_double(tok[12]);
        bar.best_ask     = safe_double(tok[13]);
        bar.open_interest= safe_double(tok[14]);

        // mark_price = 0인 행 제외 (데이터 없음)
        if (bar.mark_price <= 0.0) continue;

        ts_map[bar.timestamp].push_back(bar);
        ++row;
    }

    if (ts_map.empty())
        throw std::runtime_error("DataFeed: no valid data in " + path);

    // 타임스탬프별 MarketSnapshot 생성 (시간 오름차순 보장)
    snapshots_.reserve(ts_map.size());
    for (auto& [ts, bars] : ts_map) {
        double spot = 0.0;
        for (const auto& b : bars)
            if (b.index_price > 0) { spot = b.index_price; break; }

        // private 생성자 접근 (friend class DataFeed)
        snapshots_.emplace_back(MarketSnapshot(ts, spot, std::move(bars)));
    }

    std::cerr << "[DataFeed] Loaded " << row << " rows → "
              << snapshots_.size() << " timestamps from " << path << "\n";
}

bool DataFeed::next() {
    if (cursor_ == SIZE_MAX) {
        cursor_ = 0;
    } else {
        ++cursor_;
    }
    return cursor_ < snapshots_.size();
}

const MarketSnapshot& DataFeed::current() const {
    if (cursor_ == SIZE_MAX || cursor_ >= snapshots_.size())
        throw std::runtime_error("DataFeed: current() called before next() or after end");
    return snapshots_[cursor_];
}

size_t DataFeed::remaining() const {
    if (cursor_ == SIZE_MAX) return snapshots_.size();
    if (cursor_ >= snapshots_.size()) return 0;
    return snapshots_.size() - cursor_ - 1;
}

int64_t DataFeed::start_ts() const {
    return snapshots_.empty() ? 0 : snapshots_.front().timestamp();
}

int64_t DataFeed::end_ts() const {
    return snapshots_.empty() ? 0 : snapshots_.back().timestamp();
}

void DataFeed::reset() {
    cursor_ = SIZE_MAX;
}
