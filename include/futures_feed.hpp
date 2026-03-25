#pragma once
#include "feed_base.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────
//  futures_feed.hpp  —  선물 시계열 데이터 피드 (IFeed 구현)
//
//  prepare_futures_data.py 가 생성한 CSV 를 읽어
//  타임스탬프별 MarketSnapshot (futures 모드) 을 구성한다.
//
//  CSV 컬럼 순서:
//    timestamp, symbol,
//    open, high, low, close, volume, quote_volume,
//    funding_rate, open_interest, long_ratio, short_ratio, taker_buy_ratio
// ─────────────────────────────────────────────────────────────────

class FuturesFeed : public IFeed {
public:
    // CSV 파일 로드 (prepare_futures_data.py 가 생성한 파일)
    explicit FuturesFeed(const std::string& csv_path,
                         const std::string& spot_symbol = "BTCUSDT");

    void   reset()   override;
    bool   next()    override;
    const MarketSnapshot& current() const override;
    size_t remaining() const override;
    size_t total()     const override { return snapshots_.size(); }
    int64_t start_ts() const override;
    int64_t end_ts()   const override;

private:
    std::vector<MarketSnapshot> snapshots_;
    size_t                      cursor_      = SIZE_MAX;
    std::string                 spot_symbol_;  // spot_price() 기준 심볼

    void load_csv(const std::string& path);
};
