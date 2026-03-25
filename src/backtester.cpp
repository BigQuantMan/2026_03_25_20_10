#include "../include/backtester.hpp"
#include "../include/metrics/sharpe.hpp"
#include "../include/metrics/total_return.hpp"
#include "../include/metrics/daily_turnover.hpp"
#include <iostream>
#include <fstream>
#include <sstream>
#include <iomanip>
#include <chrono>
#include <ctime>
#include <filesystem>

namespace fs = std::filesystem;

// ── 타임스탬프 → 날짜 문자열 ─────────────────────────────────────
static std::string ts_to_date(int64_t ts_ms) {
    time_t t = ts_ms / 1000;
    char buf[32];
    struct tm* tm_info = gmtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%d", tm_info);
    return buf;
}

static std::string ts_to_datetime(int64_t ts_ms) {
    time_t t = ts_ms / 1000;
    char buf[32];
    struct tm* tm_info = gmtime(&t);
    strftime(buf, sizeof(buf), "%Y-%m-%dT%H:%M:%SZ", tm_info);
    return buf;
}

// ── 메트릭 인스턴스 생성 ──────────────────────────────────────────
std::vector<std::unique_ptr<Metric>> Backtester::create_metrics() const {
    std::vector<std::unique_ptr<Metric>> metrics;
    for (const auto& name : config_.metrics) {
        auto m = MetricRegistry::instance().create(name);
        if (m) {
            metrics.push_back(std::move(m));
        } else {
            std::cerr << "[Backtester] Unknown metric: " << name << "\n";
        }
    }
    return metrics;
}

Backtester::Backtester(BacktestConfig config)
    : config_(std::move(config)) {}

// ── 메인 실행 루프 ────────────────────────────────────────────────
BacktestResult Backtester::run(Strategy& strategy, IFeed& feed) {
    feed.reset();

    Portfolio portfolio(config_.initial_capital);
    auto metrics = create_metrics();

    BacktestResult result;
    result.strategy_name = strategy.name();
    result.start_ts      = feed.start_ts();
    result.end_ts        = feed.end_ts();

    strategy.on_start();

    std::vector<Order> pending_orders;  // t 시점에서 제출 → t+1에서 체결
    std::string last_day;
    size_t bar_count = 0;
    size_t total     = feed.total();

    while (feed.next()) {
        const MarketSnapshot& snap = feed.current();
        int64_t ts = snap.timestamp();

        // ── 날짜 변경 감지 → daily turnover 리셋 ────────────────
        std::string cur_day = ts_to_date(ts);
        if (cur_day != last_day) {
            portfolio.reset_daily();
            last_day = cur_day;
        }

        // ── Step 1: 이전 바 주문 체결 (현재 바 가격으로) ────────
        //   → look-ahead 방지: t-1에서 제출한 주문이 t 가격으로 체결
        if (!pending_orders.empty()) {
            auto filled = portfolio.fill_orders(pending_orders, snap, ts);
            if (!filled.empty())
                strategy.on_fill(filled);   // 실제 체결된 주문만 전달
            pending_orders.clear();
        }

        // ── Step 2: 시가 평가 ────────────────────────────────────
        portfolio.mark_to_market(snap, ts);

        // ── Step 3: 메트릭 업데이트 ──────────────────────────────
        for (auto& m : metrics)
            m->on_bar(ts, portfolio.state());

        // ── Step 4: equity curve 기록 (total_value: 미실현 손익 포함, mark-to-market) ──
        result.equity_curve.emplace_back(ts, portfolio.state().total_value);

        // ── Step 5: 전략 실행 → 다음 바 주문 생성 ───────────────
        //   전략은 오직 현재 스냅샷만 볼 수 있음 (DataFeed 접근 없음)
        pending_orders = strategy.on_bar(ts, snap);

        // ── 진행 상황 출력 (10% 단위, stdout) ──────────────────────
        ++bar_count;
        if (total > 0) {
            size_t pct      = bar_count * 100 / total;
            size_t prev_pct = (bar_count - 1) * 100 / total;
            if (pct != prev_pct && pct % 10 == 0) {
                std::cout << "[Progress] " << pct << "% ("
                          << bar_count << "/" << total << ")\n";
                std::cout.flush();
            }
        }
    }

    // ── 종료: 잔여 주문 무시, 전략 종료 훅 호출 ─────────────────
    strategy.on_end();

    // ── 메트릭 최종 집계 ─────────────────────────────────────────
    for (auto& m : metrics)
        result.metrics[m->name()] = m->result();

    // ── 거래 기록 복사 ────────────────────────────────────────────
    result.trades = portfolio.trades();

    return result;
}

// ── JSON 직렬화 ───────────────────────────────────────────────────
// trades 는 메인 JSON에서 제거 → .trades.jsonl 에 별도 저장
// 메인 JSON: metrics + equity_curve + total_trades + symbol_stats
// symbol_stats: 심볼별 요약 + P&L 시계열 (최대 3000 포인트 샘플링)
std::string BacktestResult::to_json() const {
    std::string j;
    j.reserve(4 * 1024 * 1024);  // 4MB 선점 (symbol_stats 포함)

    auto ap = [&](const char* s)       { j += s; };
    auto as = [&](const std::string& s){ j += s; };

    char nbuf[64];
    auto af6 = [&](double v) { snprintf(nbuf, sizeof(nbuf), "%.6f", v); j += nbuf; };
    auto af2 = [&](double v) { snprintf(nbuf, sizeof(nbuf), "%.2f", v); j += nbuf; };
    auto ai  = [&](int64_t v){ snprintf(nbuf, sizeof(nbuf), "%lld", (long long)v); j += nbuf; };

    ap("{\n");
    ap("  \"strategy\": \""); as(strategy_name); ap("\",\n");
    ap("  \"period\": {\n");
    ap("    \"start\": \""); as(ts_to_datetime(start_ts)); ap("\",\n");
    ap("    \"end\":   \""); as(ts_to_datetime(end_ts));   ap("\"\n");
    ap("  },\n");

    // 메트릭
    ap("  \"metrics\": {\n");
    bool first = true;
    for (const auto& kv : metrics) {
        if (!first) ap(",\n");
        ap("    \""); as(kv.first); ap("\": "); af6(kv.second);
        first = false;
    }
    ap("\n  },\n");

    // equity curve (15분 샘플링)
    ap("  \"equity_curve\": [\n");
    const int64_t INTERVAL_MS = 15LL * 60 * 1000;
    int64_t last_written_ts = -1;
    bool first_pt = true;
    for (size_t i = 0; i < equity_curve.size(); ++i) {
        const bool is_last = (i + 1 == equity_curve.size());
        const int64_t ts   = equity_curve[i].first;
        if (last_written_ts < 0 || ts - last_written_ts >= INTERVAL_MS || is_last) {
            if (!first_pt) ap(",\n");
            ap("    ["); ai(ts); ap(", "); af2(equity_curve[i].second); ap("]");
            last_written_ts = ts;
            first_pt = false;
        }
    }
    ap("\n  ],\n");

    // 거래 요약 (카운트만, 상세는 .trades.jsonl)
    size_t long_cnt = 0, short_cnt = 0;
    for (const auto& t : trades) {
        if (t.side == Side::LONG) ++long_cnt; else ++short_cnt;
    }
    ap("  \"total_trades\": "); ai((int64_t)trades.size()); ap(",\n");
    ap("  \"long_trades\":  "); ai((int64_t)long_cnt);      ap(",\n");
    ap("  \"short_trades\": "); ai((int64_t)short_cnt);     ap(",\n");

    // ── 심볼별 통계 (symbol_stats) ────────────────────────────
    struct SymStat {
        const char* instr = "UNKNOWN";
        int64_t trade_cnt = 0, longs = 0, shorts = 0;
        int64_t closed = 0, wins = 0, losses = 0;
        double  total_pnl = 0, best_pnl = 0, worst_pnl = 0;
        bool    first_pnl = true;
        // pnl_series: (ts, cumulative_pnl) — 시간순, 나중에 샘플링
        std::vector<std::pair<int64_t, double>> pnl_pts;
    };

    std::unordered_map<std::string, SymStat> sym_map;

    for (const auto& t : trades) {
        const std::string& key = t.symbol.empty() ? strategy_name : t.symbol;
        const char* instr_s = (t.instrument == Instrument::FUTURE) ? "FUTURE" :
                              (t.instrument == Instrument::OPTION) ? "OPTION"  : "SPOT";

        auto& s = sym_map[key];
        s.instr = instr_s;
        ++s.trade_cnt;
        if (t.side == Side::LONG) ++s.longs; else ++s.shorts;

        if (t.pnl != 0.0) {
            ++s.closed;
            s.total_pnl += t.pnl;
            if (t.pnl > 0) ++s.wins; else ++s.losses;

            if (s.first_pnl) {
                s.best_pnl = s.worst_pnl = t.pnl;
                s.first_pnl = false;
            } else {
                if (t.pnl > s.best_pnl)  s.best_pnl  = t.pnl;
                if (t.pnl < s.worst_pnl) s.worst_pnl = t.pnl;
            }

            double cum = s.pnl_pts.empty() ? 0 : s.pnl_pts.back().second;
            s.pnl_pts.emplace_back(t.timestamp, cum + t.pnl);
        }
    }

    ap("  \"symbol_stats\": {");
    bool first_sym = true;
    for (const auto& [sym, s] : sym_map) {
        if (!first_sym) ap(",");
        first_sym = false;
        ap("\n    \""); as(sym); ap("\": {");
        ap("\n      \"instr\": \"");          ap(s.instr);             ap("\",");
        ap("\n      \"trades\": ");           ai(s.trade_cnt);         ap(",");
        ap("\n      \"longs\": ");            ai(s.longs);             ap(",");
        ap("\n      \"shorts\": ");           ai(s.shorts);            ap(",");
        ap("\n      \"closed_trades\": ");    ai(s.closed);            ap(",");
        ap("\n      \"wins\": ");             ai(s.wins);              ap(",");
        ap("\n      \"losses\": ");           ai(s.losses);            ap(",");
        ap("\n      \"total_pnl\": ");        af2(s.total_pnl);        ap(",");
        ap("\n      \"best_pnl\": ");         af2(s.best_pnl);         ap(",");
        ap("\n      \"worst_pnl\": ");        af2(s.worst_pnl);        ap(",");

        // pnl_series: 최대 3000 포인트로 샘플링
        const auto& pts = s.pnl_pts;
        const size_t n  = pts.size();
        ap("\n      \"pnl_series\": [");
        if (n > 0) {
            const size_t MAX_PTS = 3000;
            size_t step = (n <= MAX_PTS) ? 1 : (n / MAX_PTS);
            bool first_p = true;
            for (size_t i = 0; i < n; i += step) {
                if (!first_p) ap(",");
                first_p = false;
                ap("["); ai(pts[i].first); ap(","); af2(pts[i].second); ap("]");
            }
            // 항상 마지막 포인트 포함
            if (n > 1 && (n - 1) % step != 0) {
                ap(",["); ai(pts[n-1].first); ap(","); af2(pts[n-1].second); ap("]");
            }
        }
        ap("]\n    }");
    }
    ap("\n  }\n}\n");
    return j;
}

void BacktestResult::print_summary() const {
    std::cout << "\n══════════════════════════════════════\n";
    std::cout << "  Backtest: " << strategy_name << "\n";
    std::cout << "  Period  : " << ts_to_date(start_ts)
              << " ~ " << ts_to_date(end_ts) << "\n";
    std::cout << "  Trades  : " << trades.size() << "\n";
    std::cout << "──────────────────────────────────────\n";
    for (const auto& kv : metrics)
        std::cout << "  " << kv.first << ": " << kv.second << "\n";
    std::cout << "══════════════════════════════════════\n\n";
}

// ── Full equity curve (모든 바, 확대 뷰용) ────────────────────────
std::string BacktestResult::full_equity_json() const {
    const size_t n = equity_curve.size();
    std::string j;
    j.reserve(n * 28 + 32);  // ts(13) + ',' + eq(10) + overhead

    char buf[32];
    j += "{\"ts\":[";
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) j += ',';
        snprintf(buf, sizeof(buf), "%lld", (long long)equity_curve[i].first);
        j += buf;
    }
    j += "],\"eq\":[";
    for (size_t i = 0; i < n; ++i) {
        if (i > 0) j += ',';
        snprintf(buf, sizeof(buf), "%.2f", equity_curve[i].second);
        j += buf;
    }
    j += "]}";
    return j;
}

void Backtester::save_results(const BacktestResult& result) const {
    fs::create_directories(config_.output_dir);

    // 파일명: results/STRATEGYNAME_YYYYMMDD_HHMMSS.json
    auto now = std::chrono::system_clock::now();
    time_t t = std::chrono::system_clock::to_time_t(now);
    char buf[32];
    struct tm* tm_info = gmtime(&t);
    strftime(buf, sizeof(buf), "%Y%m%d_%H%M%S", tm_info);

    std::string stem = result.strategy_name + "_" + buf;

    // ── 메인 결과 JSON (trades 제외, symbol_stats 포함) ──────────
    std::string path = config_.output_dir + "/" + stem + ".json";
    std::ofstream f(path);
    if (!f.is_open())
        throw std::runtime_error("Cannot write result: " + path);
    f << result.to_json();
    std::cout << "[Backtester] Results saved to " << path << "\n";

    // ── 전체 equity curve (.eq.json, 확대 뷰에서 1분 단위 조회) ──
    std::string eq_path = config_.output_dir + "/" + stem + ".eq.json";
    std::ofstream eq_f(eq_path);
    if (eq_f.is_open())
        eq_f << result.full_equity_json();

    // ── 거래 기록 JSON Lines (.trades.jsonl, 트레이드 테이블 페이지네이션용) ──
    // 한 줄 = 한 거래 → 백엔드에서 tail-read 로 최신 N개 즉시 반환 가능
    std::string trades_path = config_.output_dir + "/" + stem + ".trades.jsonl";
    std::ofstream tf(trades_path);
    if (tf.is_open()) {
        char line[512];
        for (const auto& tr : result.trades) {
            const char* instr = (tr.instrument == Instrument::OPTION) ? "OPTION" :
                                (tr.instrument == Instrument::FUTURE) ? "FUTURE" : "SPOT";
            const char* side  = (tr.side == Side::LONG) ? "LONG" : "SHORT";

            int n = snprintf(line, sizeof(line),
                "{\"ts\":%lld,\"instr\":\"%s\",\"side\":\"%s\","
                "\"qty\":%.6f,\"price\":%.2f,\"pnl\":%.2f",
                (long long)tr.timestamp, instr, side,
                tr.quantity, tr.price, tr.pnl);
            tf.write(line, n);

            if (!tr.symbol.empty()) {
                tf << ",\"symbol\":\"" << tr.symbol << "\"";
                if (tr.instrument == Instrument::OPTION) {
                    tf << ",\"strike\":" << tr.strike
                       << ",\"type\":\"" << tr.option_type << "\""
                       << ",\"expiry\":\"" << tr.expiry << "\"";
                }
            }
            tf << "}\n";
        }
    }
}
