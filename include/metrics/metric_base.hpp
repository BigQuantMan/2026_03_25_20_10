#pragma once
#include "../portfolio.hpp"
#include <string>
#include <memory>
#include <functional>
#include <unordered_map>

// ─────────────────────────────────────────────────────────────────
//  metric_base.hpp  —  커스텀 메트릭 확장 인터페이스
//
//  기본 제공 메트릭: SharpeRatio, TotalReturn, DailyTurnover
//
//  커스텀 메트릭 추가 방법:
//    1. Metric을 상속하는 클래스 작성
//    2. metrics/ 폴더에 .hpp/.cpp 파일 추가
//    3. 파일 맨 아래에 REGISTER_METRIC("이름", 클래스명) 한 줄 추가
//    4. Makefile의 SRCS에 .cpp 경로 추가
//
//  예시:
//    class MaxDrawdown : public Metric {
//    public:
//        void on_bar(int64_t ts, const PortfolioState& s) override { ... }
//        double result() const override { return max_dd_; }
//        std::string name() const override { return "max_drawdown"; }
//    private:
//        double peak_ = 0, max_dd_ = 0;
//    };
//    REGISTER_METRIC("max_drawdown", MaxDrawdown)
// ─────────────────────────────────────────────────────────────────

class Metric {
public:
    virtual ~Metric() = default;

    // 매 바마다 상태 업데이트
    virtual void on_bar(int64_t ts, const PortfolioState& state) = 0;

    // 최종 결과값 반환
    virtual double result() const = 0;

    // 결과 파일에 기록될 키 이름
    virtual std::string name() const = 0;
};

// ─────────────────────────────────────────────────────────────────
//  MetricRegistry  —  메트릭 자동 등록 + 생성
// ─────────────────────────────────────────────────────────────────

class MetricRegistry {
public:
    static MetricRegistry& instance() {
        static MetricRegistry inst;
        return inst;
    }

    using Factory = std::function<std::unique_ptr<Metric>()>;

    void register_metric(const std::string& name, Factory factory) {
        factories_[name] = std::move(factory);
    }

    std::unique_ptr<Metric> create(const std::string& name) const {
        auto it = factories_.find(name);
        if (it == factories_.end()) return nullptr;
        return it->second();
    }

    std::vector<std::string> all_names() const {
        std::vector<std::string> names;
        for (const auto& kv : factories_) names.push_back(kv.first);
        return names;
    }

private:
    std::unordered_map<std::string, Factory> factories_;
};

// ─────────────────────────────────────────────────────────────────
//  REGISTER_METRIC 매크로
//  파일 전역 영역에서 한 줄로 자동 등록
// ─────────────────────────────────────────────────────────────────
#define REGISTER_METRIC(metric_name, cls)                                  \
    static bool _registered_##cls = []() {                                 \
        MetricRegistry::instance().register_metric(                        \
            metric_name,                                                   \
            []() -> std::unique_ptr<Metric> {                              \
                return std::make_unique<cls>();                            \
            }                                                              \
        );                                                                 \
        return true;                                                       \
    }();
