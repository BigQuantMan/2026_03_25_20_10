#pragma once
#include "feed_base.hpp"
#include <vector>
#include <string>
#include <stdexcept>

// ─────────────────────────────────────────────────────────────────
//  data_feed.hpp  —  옵션 시계열 데이터 피드 (IFeed 구현)
//
//  Look-ahead Bias 차단의 핵심:
//    - next() 만 존재. 뒤로 돌아가거나 미래로 점프 불가
//    - Strategy는 DataFeed에 대한 참조를 받지 않음
//    - Backtester만 DataFeed를 소유하고 호출
// ─────────────────────────────────────────────────────────────────

class DataFeed : public IFeed {
public:
    // CSV 파일 로드 (prepare_data.py가 생성한 파일)
    explicit DataFeed(const std::string& csv_path);

    void   reset()   override;
    bool   next()    override;
    const MarketSnapshot& current() const override;
    size_t remaining() const override;
    size_t total()     const override { return snapshots_.size(); }
    int64_t start_ts() const override;
    int64_t end_ts()   const override;

private:
    std::vector<MarketSnapshot> snapshots_;
    size_t                      cursor_ = SIZE_MAX;  // SIZE_MAX = 아직 시작 전

    // CSV 파싱 헬퍼
    void load_csv(const std::string& path);
};
