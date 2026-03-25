#pragma once
#include "strategy.hpp"
#include "feed_base.hpp"
#include "portfolio.hpp"
#include "metrics/metric_base.hpp"
#include <memory>
#include <vector>
#include <string>

// ─────────────────────────────────────────────────────────────────
//  backtester.hpp  —  백테스트 엔진 오케스트레이터
//
//  실행 순서 (매 바):
//    1. DataFeed.next() → 현재 바 스냅샷 (t)
//    2. 이전 바(t-1) 주문 체결 → t 봉 open 가격으로 체결 (look-ahead 완전 차단)
//       ※ 전략은 t-1 close까지만 봤으므로, t open은 미래가 아닌 최초 접근 가능 가격
//    3. Portfolio.mark_to_market(t)  ← t 봉 close 기준 평가
//    4. Metric.on_bar(t, portfolio_state)
//    5. Strategy.on_bar(t, snapshot) → 새 주문 목록 (→ t+1 open에서 체결)
//
//  결과 저장: results/ 폴더에 JSON 출력
// ─────────────────────────────────────────────────────────────────

struct BacktestConfig {
    double      initial_capital  = 10000.0;  // USDT
    bool        verbose          = false;    // 진행 상황 출력
    std::string output_dir       = "results";
    double      slippage_bps     = 0.0;      // 슬리피지 (basis points) — TODO: 실제 체결 계산에 반영 예정

    // 메트릭 목록 (기본 3개 + 커스텀)
    std::vector<std::string> metrics = {
        "sharpe", "total_return_pct", "avg_daily_turnover"
    };
};

struct BacktestResult {
    std::string strategy_name;
    int64_t     start_ts;
    int64_t     end_ts;

    // 최종 메트릭
    std::unordered_map<std::string, double> metrics;

    // 시계열 (equity curve)
    std::vector<std::pair<int64_t, double>> equity_curve;

    // 거래 기록
    std::vector<Trade> trades;

    // JSON 직렬화 (15분 샘플링)
    std::string to_json() const;

    // 전체 equity curve (1분/1시간 단위, 확대 뷰용)
    std::string full_equity_json() const;

    // 콘솔 출력
    void print_summary() const;
};

class Backtester {
public:
    explicit Backtester(BacktestConfig config = {});

    // 백테스트 실행 (DataFeed 또는 FuturesFeed 모두 사용 가능)
    BacktestResult run(Strategy& strategy, IFeed& feed);

    // 결과 저장 (JSON)
    void save_results(const BacktestResult& result) const;

private:
    BacktestConfig config_;

    // 메트릭 인스턴스 생성
    std::vector<std::unique_ptr<Metric>> create_metrics() const;
};
