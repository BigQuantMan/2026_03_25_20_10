#pragma once
#include "market_data.hpp"
#include "types.hpp"
#include <deque>
#include <unordered_map>
#include <string>

// ─────────────────────────────────────────────────────────────────
//  bar_matrix.hpp  —  전략 개발용 시계열 행렬 유틸리티
//
//  개념:
//    BarMatrix = 시간축(행) × 심볼축(열) 의 슬라이딩 윈도우
//
//    update(snap) 를 on_bar() 첫 줄에 호출하면,
//    매 바마다 자동으로 최신 데이터가 쌓입니다.
//
//    get_column(m, n) 으로 n바 전 스냅샷(모든 심볼의 값)을 꺼냅니다.
//
//  ── 사용 예 ──────────────────────────────────────────────────────
//
//  class MyStrategy : public Strategy {
//  public:
//      BarMatrix close_m { Field::Close };
//      BarMatrix fund_m  { Field::FundingRate, 200 };
//
//      std::vector<Order> on_bar(int64_t ts, const MarketSnapshot& snap) override {
//          close_m.update(snap);
//          fund_m.update(snap);
//
//          if (!close_m.ready(20)) return {};   // 워밍업 대기
//
//          auto cur  = get_column(close_m, 0);  // 현재 바 (t)
//          auto prev = get_column(close_m, 1);  // 1바 전  (t-1)
//
//          // B 스타일: 전 심볼 한 번에 연산
//          auto ret = (cur - prev) / prev;      // 심볼별 1봉 수익률
//
//          // A 스타일: 특정 심볼만
//          double btc = cur["BTCUSDT"];
//
//          // 반복
//          for (const auto& [sym, r] : ret) { ... }
//
//          return orders;
//      }
//  };
//
//  ※ look-ahead 안전: update(snap) 는 on_bar() 진입 후 전략이
//    직접 호출하므로, get_column(m, 0) 은 현재 바(t) 데이터만 담습니다.
// ─────────────────────────────────────────────────────────────────


// ── Field: BarMatrix에 담을 FuturesBar 필드 선택 ─────────────────
enum class Field {
    Open,           // 시가
    High,           // 고가
    Low,            // 저가
    Close,          // 종가
    Volume,         // 거래량 (base)
    QuoteVolume,    // 거래량 (USDT)
    FundingRate,    // 펀딩비
    OpenInterest,   // 미결제약정
    LongRatio,      // 롱 비율
    ShortRatio,     // 숏 비율
    TakerBuyRatio,  // 테이커 매수 비율
};


// ─────────────────────────────────────────────────────────────────
//  Column  —  get_column() 의 반환 타입
//
//  t 시점의 모든 심볼 값을 담은 스냅샷.
//  map 스타일 접근과 산술 연산을 동시에 지원합니다.
// ─────────────────────────────────────────────────────────────────
struct Column {
    std::unordered_map<std::string, double> data;

    Column() = default;
    explicit Column(std::unordered_map<std::string, double> d)
        : data(std::move(d)) {}

    // ── map 스타일 접근 ───────────────────────────────────────────
    // 없는 심볼은 0.0 반환
    double operator[](const std::string& sym) const {
        auto it = data.find(sym);
        return it != data.end() ? it->second : 0.0;
    }

    bool   contains(const std::string& sym) const { return data.count(sym) > 0; }
    size_t size()  const { return data.size(); }
    bool   empty() const { return data.empty(); }

    // ── 반복 ──────────────────────────────────────────────────────
    auto begin() const { return data.begin(); }
    auto end()   const { return data.end(); }

    // ── Column × Column 산술 (심볼 교집합 기준) ──────────────────
    Column operator+(const Column& rhs) const {
        return apply(rhs, [](double a, double b){ return a + b; });
    }
    Column operator-(const Column& rhs) const {
        return apply(rhs, [](double a, double b){ return a - b; });
    }
    Column operator*(const Column& rhs) const {
        return apply(rhs, [](double a, double b){ return a * b; });
    }
    Column operator/(const Column& rhs) const {
        return apply(rhs, [](double a, double b){
            return (b != 0.0) ? a / b : 0.0;
        });
    }

    // ── Column × scalar 산술 ─────────────────────────────────────
    Column operator+(double s) const { return apply_scalar([s](double a){ return a + s; }); }
    Column operator-(double s) const { return apply_scalar([s](double a){ return a - s; }); }
    Column operator*(double s) const { return apply_scalar([s](double a){ return a * s; }); }
    Column operator/(double s) const {
        return apply_scalar([s](double a){ return (s != 0.0) ? a / s : 0.0; });
    }

private:
    template<typename F>
    Column apply(const Column& rhs, F func) const {
        Column result;
        for (const auto& [sym, val] : data) {
            auto it = rhs.data.find(sym);
            if (it != rhs.data.end())
                result.data[sym] = func(val, it->second);
        }
        return result;
    }

    template<typename F>
    Column apply_scalar(F func) const {
        Column result;
        for (const auto& [sym, val] : data)
            result.data[sym] = func(val);
        return result;
    }
};

// scalar × Column (교환법칙)
inline Column operator+(double s, const Column& col) { return col + s; }
inline Column operator*(double s, const Column& col) { return col * s; }


// ─────────────────────────────────────────────────────────────────
//  BarMatrix  —  슬라이딩 윈도우 행렬
//
//  내부 구조:
//    history_[0]       = 가장 오래된 바
//    history_.back()   = 가장 최근 바 (= get_column(m, 0))
// ─────────────────────────────────────────────────────────────────
class BarMatrix {
public:
    // field     : 저장할 FuturesBar 필드 (Field::Close 등)
    // max_history : 최대 보관 바 수 (기본 500)
    explicit BarMatrix(Field field, int max_history = 500)
        : field_(field), max_history_(max_history) {}

    // ── on_bar() 첫 줄에 호출 ────────────────────────────────────
    // 현재 스냅샷의 모든 선물 심볼 값을 추출해 히스토리에 추가합니다.
    void update(const MarketSnapshot& snap) {
        std::unordered_map<std::string, double> row;
        for (const auto& [sym, bar] : snap.all_futures()) {
            row[sym] = extract(bar);
        }
        history_.push_back(std::move(row));
        if ((int)history_.size() > max_history_)
            history_.pop_front();
    }

    // ── 워밍업 체크 ───────────────────────────────────────────────
    // min_bars 이상 쌓여야 신호 계산 가능한 경우에 사용
    // 예: if (!close_m.ready(20)) return {};
    bool ready(int min_bars = 1) const {
        return (int)history_.size() >= min_bars;
    }

    // 현재까지 쌓인 바 수
    int size() const { return (int)history_.size(); }

    Field field() const { return field_; }

private:
    Field  field_;
    int    max_history_;
    std::deque<std::unordered_map<std::string, double>> history_;

    // get_column 이 private history_ 에 직접 접근 허용
    friend Column get_column(const BarMatrix& m, int offset);

    double extract(const FuturesBar& bar) const {
        switch (field_) {
            case Field::Open:          return bar.open;
            case Field::High:          return bar.high;
            case Field::Low:           return bar.low;
            case Field::Close:         return bar.close;
            case Field::Volume:        return bar.volume;
            case Field::QuoteVolume:   return bar.quote_volume;
            case Field::FundingRate:   return bar.funding_rate;
            case Field::OpenInterest:  return bar.open_interest;
            case Field::LongRatio:     return bar.long_ratio;
            case Field::ShortRatio:    return bar.short_ratio;
            case Field::TakerBuyRatio: return bar.taker_buy_ratio;
        }
        return 0.0;
    }
};


// ─────────────────────────────────────────────────────────────────
//  get_column(matrix, offset)
//
//  offset = 0 : 현재 바 (t)      ← on_bar() 안에서 update() 후 호출
//  offset = 1 : 1바 전 (t-1)
//  offset = n : n바 전 (t-n)
//
//  범위 초과 시 빈 Column{} 반환 (ready() 로 사전 체크 권장)
// ─────────────────────────────────────────────────────────────────
inline Column get_column(const BarMatrix& m, int offset) {
    if (offset < 0 || offset >= (int)m.history_.size())
        return Column{};
    int idx = (int)m.history_.size() - 1 - offset;
    return Column(m.history_[idx]);
}
