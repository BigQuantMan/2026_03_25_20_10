#pragma once
#include "feed_base.hpp"
#include <vector>
#include <string>
#include <unordered_map>
#include <climits>

// ─────────────────────────────────────────────────────────────────
//  unified_feed.hpp  —  옵션 + 선물 통합 데이터 피드 (IFeed 구현)
//
//  설계 원칙:
//    - options_csv, futures_csv 둘 다 또는 하나만 넘길 수 있음
//    - 타임라인 = 두 소스의 타임스탬프 합집합 (union)
//    - 더 희소한 쪽은 앞 값으로 forward-fill
//    - MarketSnapshot 에 options_ / futures_ 모두 채움
//
//  최적화:
//    - CSV 로딩 시점에 date range 필터 적용 (OOM 방지)
//    - 스냅샷 lazy 빌드 + std::move (startup 빠름)
//    - FILE* + 8MB 버퍼 + in-place split (stringstream 제거)
// ─────────────────────────────────────────────────────────────────

class UnifiedFeed : public IFeed {
public:
    explicit UnifiedFeed(const std::string& options_csv  = "",
                         const std::string& futures_csv  = "",
                         const std::string& spot_symbol  = "BTCUSDT",
                         int64_t            start_ts_ms  = 0,
                         int64_t            end_ts_ms    = INT64_MAX);

    void   reset()   override;
    bool   next()    override;
    const MarketSnapshot& current() const override;
    size_t remaining() const override;
    size_t total()     const override { return all_ts_.size(); }
    int64_t start_ts() const override;
    int64_t end_ts()   const override;

    bool has_options() const { return has_options_; }
    bool has_futures() const { return has_futures_; }

private:
    // ── 타임라인 ─────────────────────────────────────────────────
    std::vector<int64_t> all_ts_;   // 정렬된 타임스탬프 목록
    size_t               cursor_ = SIZE_MAX;

    // ── 로드된 원본 데이터 (lazy 소비용 — next() 에서 move로 꺼냄) ──
    std::unordered_map<int64_t,
        std::unordered_map<std::string, FuturesBar>> futs_map_;
    std::unordered_map<int64_t,
        std::vector<OptionBar>>                       opts_map_;

    // ── forward-fill 상태 ─────────────────────────────────────────
    std::vector<OptionBar>                             last_opts_;

    // ── 현재 스냅샷 (next() 가 lazy하게 빌드) ─────────────────────
    MarketSnapshot current_snap_;   // 기본 생성자로 초기화 가능

    // ── 기타 ─────────────────────────────────────────────────────
    std::string spot_symbol_;
    bool        has_options_ = false;
    bool        has_futures_ = false;
    int64_t     start_ts_filter_ = 0;
    int64_t     end_ts_filter_   = INT64_MAX;

    void build(const std::string& options_csv,
               const std::string& futures_csv);
};
