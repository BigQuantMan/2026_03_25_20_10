#pragma once
#include "types.hpp"
#include "market_data.hpp"
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────
//  strategy.hpp  —  전략 제작자가 상속하는 베이스 클래스
//
//  ┌──────────────────────────────────────────────────────────┐
//  │  전략 작성 규칙 (MUST READ)                              │
//  │                                                          │
//  │  1. on_bar() 안에서 미래 데이터를 저장/참조하면 안 됨   │
//  │     → snapshot은 현재 시점까지만의 데이터               │
//  │                                                          │
//  │  2. on_bar()가 반환하는 Order는 다음 바에서 체결됨       │
//  │     → 현재 바의 종가를 보고 즉시 체결하는 것 불가       │
//  │                                                          │
//  │  3. quantity는 반드시 > 0  (방향은 Side로 구분)         │
//  │                                                          │
//  │  4. 전략 내부 상태(멤버 변수)는 자유롭게 사용 가능      │
//  │     → 과거 데이터 저장, 지표 계산 등은 모두 허용        │
//  └──────────────────────────────────────────────────────────┘
//
//  최소 구현 예시:
//
//    class MyStrategy : public Strategy {
//    public:
//        std::string name() const override { return "MyStrategy"; }
//
//        std::vector<Order> on_bar(int64_t ts,
//                                  const MarketSnapshot& snap) override
//        {
//            // snap.spot_price(), snap.calls(), snap.get(...) 등 사용
//            // ...계산...
//            return { Order::buy_call(90000, "2026-02-28", 0.01) };
//        }
//    };
// ─────────────────────────────────────────────────────────────────

class Strategy {
public:
    virtual ~Strategy() = default;

    // ── 필수 구현 ─────────────────────────────────────────────

    // 전략 이름 (결과 파일에 기록됨)
    virtual std::string name() const = 0;

    // 매 바마다 호출. 주문 목록을 반환.
    // - timestamp : 현재 바의 Unix ms (UTC)
    // - snap      : 현재 시점까지만의 불변 시장 스냅샷
    // - return    : 제출할 주문 목록 (없으면 빈 벡터 반환)
    virtual std::vector<Order> on_bar(
        int64_t               timestamp,
        const MarketSnapshot& snap
    ) = 0;

    // ── 선택 구현 ─────────────────────────────────────────────

    // 백테스트 시작 시 1회 호출 (초기화 용도)
    virtual void on_start() {}

    // 백테스트 종료 시 1회 호출 (정리 용도)
    virtual void on_end() {}

    // 주문 체결 결과 콜백 (t+1 체결 직후 호출)
    // - filled : 실제로 체결된 주문 목록 (fill price = 0이거나 현금 부족 등으로
    //            skip된 주문은 포함되지 않음)
    // 전략이 포지션 상태를 정확히 추적하려면 이 콜백을 구현할 것.
    virtual void on_fill(const std::vector<Order>& /*filled*/) {}
};
