#pragma once
#include "market_data.hpp"
#include <cstddef>
#include <cstdint>

// ─────────────────────────────────────────────────────────────────
//  feed_base.hpp  —  DataFeed / FuturesFeed 공통 인터페이스
//
//  Backtester::run() 은 IFeed& 를 받아 작동하므로
//  두 피드 모두 같은 엔진으로 실행 가능.
// ─────────────────────────────────────────────────────────────────

class IFeed {
public:
    virtual ~IFeed() = default;

    // 처음으로 되돌리기 (재실행용)
    virtual void reset() = 0;

    // 다음 바로 이동. 남은 데이터가 없으면 false 반환
    virtual bool next() = 0;

    // 현재 바의 스냅샷 (next() 호출 후에만 유효)
    virtual const MarketSnapshot& current() const = 0;

    // 남은 바 수 (진행률 표시용)
    virtual size_t remaining() const = 0;

    // 전체 바 수
    virtual size_t total() const = 0;

    // 전체 타임스탬프 범위
    virtual int64_t start_ts() const = 0;
    virtual int64_t end_ts()   const = 0;
};
