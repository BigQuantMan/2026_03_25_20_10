#include "include/backtester.hpp"
#include "include/unified_feed.hpp"
#include <ctime>
#include <climits>

// ── 전략 include ─────────────────────────────────────────────────
#include "strategies/example_pcp.hpp"
#include "strategies/example_futures.hpp"
#include "strategies/golden_cross.hpp"
#include "strategies/short_strangle.hpp"
#include "strategies/iv_futures_signal.hpp"

#include <iostream>
#include <string>

void print_usage(const char* prog) {
    std::cout << "Usage: " << prog << " [options]\n"
              << "\n"
              << "데이터 선택 (하나 이상 필수):\n"
              << "  --options-csv <path>   옵션 데이터 CSV\n"
              << "  --futures-csv <path>   선물 데이터 CSV\n"
              << "\n"
              << "  [하위 호환] 첫 번째 인자를 options-csv 로 간주:\n"
              << "    " << prog << " is_data.csv --capital 10000\n"
              << "\n"
              << "실행 옵션:\n"
              << "  --capital <USDT>       초기 자본 (기본: 10000)\n"
              << "  --verbose              진행 상황 출력\n"
              << "  --output <dir>         결과 저장 폴더 (기본: results)\n"
              << "  --spot <symbol>        spot_price() 기준 심볼 (기본: BTCUSDT)\n"
              << "  --strategy <name>      실행할 전략 이름 (기본: auto)\n"
              << "\n"
              << "예시:\n"
              << "  # 선물 전략\n"
              << "  " << prog << " --futures-csv results/prepared/futures_is_data.csv --capital 10000 --verbose\n"
              << "\n"
              << "  # 옵션 전략\n"
              << "  " << prog << " --options-csv results/prepared/is_data.csv --capital 10000 --verbose\n"
              << "\n"
              << "  # 옵션 + 선물 통합\n"
              << "  " << prog << " --options-csv results/prepared/is_data.csv \\\n"
              << "             --futures-csv results/prepared/futures_is_data.csv --verbose\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_usage(argv[0]);
        return 1;
    }

    std::string options_csv;
    std::string futures_csv;
    double      capital      = 10000.0;
    bool        verbose      = false;
    std::string output_dir   = "results";
    std::string spot_sym     = "BTCUSDT";
    std::string strategy     = "";   // 명시 필수. 단일 데이터 소스일 때만 auto
    double      slippage_bps = 0.0;  // TODO: 실제 체결 계산 반영 예정
    int64_t     start_ts_ms  = 0;
    int64_t     end_ts_ms    = INT64_MAX;

    // YYYY-MM-DD 문자열 → Unix ms 변환 헬퍼
    auto parse_date = [](const std::string& s, bool end_of_day) -> int64_t {
        struct tm t = {};
        // strptime 이식성: %Y-%m-%d 파싱
        int y=0, mo=0, d=0;
        if (sscanf(s.c_str(), "%d-%d-%d", &y, &mo, &d) != 3) return 0;
        t.tm_year = y - 1900; t.tm_mon = mo - 1; t.tm_mday = d;
        t.tm_hour = end_of_day ? 23 : 0;
        t.tm_min  = end_of_day ? 59 : 0;
        t.tm_sec  = end_of_day ? 59 : 0;
        t.tm_isdst = 0;
#ifdef _WIN32
        time_t tt = _mkgmtime(&t);
#else
        time_t tt = timegm(&t);
#endif
        return (int64_t)tt * 1000LL + (end_of_day ? 999 : 0);
    };

    // 첫 번째 인자가 '--' 로 시작하지 않으면 하위 호환 options-csv 로 간주
    if (argc >= 2 && argv[1][0] != '-') {
        options_csv = argv[1];
    }

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--options-csv" && i+1 < argc) { options_csv   = argv[++i]; }
        if (arg == "--futures-csv" && i+1 < argc) { futures_csv   = argv[++i]; }
        if (arg == "--capital"     && i+1 < argc) { capital       = std::stod(argv[++i]); }
        if (arg == "--verbose")                   { verbose       = true; }
        if (arg == "--output"      && i+1 < argc) { output_dir    = argv[++i]; }
        if (arg == "--spot"        && i+1 < argc) { spot_sym      = argv[++i]; }
        if (arg == "--strategy"    && i+1 < argc) { strategy      = argv[++i]; }
        if (arg == "--slippage"    && i+1 < argc) { slippage_bps  = std::stod(argv[++i]); }
        if (arg == "--start"       && i+1 < argc) { start_ts_ms   = parse_date(argv[++i], false); }
        if (arg == "--end"         && i+1 < argc) { end_ts_ms     = parse_date(argv[++i], true);  }
        // 하위 호환 플래그들
        if (arg == "--mode"        && i+1 < argc) { ++i; } // 무시
        if (arg == "--entry"       && i+1 < argc) { ++i; } // 무시
        if (arg == "--exit"        && i+1 < argc) { ++i; } // 무시
        if (arg == "--expiry"      && i+1 < argc) { ++i; } // 무시
    }

    if (options_csv.empty() && futures_csv.empty()) {
        std::cerr << "Error: --options-csv 또는 --futures-csv 중 하나 이상 필요\n\n";
        print_usage(argv[0]);
        return 1;
    }

    // ── 시작 배너 (데이터 로드 전에 출력 — cerr은 unbuffered) ──────
    std::cerr << "══════════════════════════════════════════\n";
    std::cerr << "  Binance Backtester\n";
    if (!options_csv.empty()) std::cerr << "  Options : " << options_csv << "\n";
    if (!futures_csv.empty()) std::cerr << "  Futures : " << futures_csv << "\n";
    std::cerr << "  Capital : " << capital  << " USDT\n";
    if (start_ts_ms > 0 || end_ts_ms != INT64_MAX) {
        // 날짜 범위 표시 (ms → YYYY-MM-DD)
        auto fmt_ts = [](int64_t ms) -> std::string {
            if (ms <= 0 || ms == INT64_MAX) return "—";
            time_t t = ms / 1000;
            char buf[16]; struct tm* tm = gmtime(&t);
            strftime(buf, sizeof(buf), "%Y-%m-%d", tm);
            return buf;
        };
        std::cerr << "  Period  : " << fmt_ts(start_ts_ms)
                  << " ~ " << fmt_ts(end_ts_ms) << "\n";
    }
    std::cerr << "══════════════════════════════════════════\n";
    std::cerr << "[Backtester] Loading data...\n";

    // ── 데이터 로드 ───────────────────────────────────────────────
    UnifiedFeed feed(options_csv, futures_csv, spot_sym, start_ts_ms, end_ts_ms);

    bool use_futures = feed.has_futures();
    bool use_options = feed.has_options();

    // ── 전략 자동 추론 (단일 소스일 때만) ──────────────────────────
    if (strategy.empty()) {
        if (use_futures && !use_options) {
            strategy = "FundingMomentum";
        } else if (use_options && !use_futures) {
            strategy = "PutCallParity";
        } else {
            std::cerr << "Error: 두 데이터 소스가 모두 로드된 경우 --strategy를 명시해야 합니다.\n"
                      << "  사용 가능: FundingMomentum, MultiSymbolMomentum, PutCallParity\n";
            return 1;
        }
    }

    std::cerr << "[Backtester] Strategy: " << strategy << "\n";
    std::cerr << "[Backtester] Starting backtest...\n";

    BacktestConfig config;
    config.initial_capital = capital;
    config.verbose         = verbose;
    config.output_dir      = output_dir;
    config.slippage_bps    = slippage_bps;
    config.metrics         = { "sharpe", "total_return_pct", "avg_daily_turnover" };

    Backtester bt(config);
    BacktestResult result;

    if (strategy == "FundingMomentum") {
        FundingMomentumStrategy s;
        result = bt.run(s, feed);
    } else if (strategy == "MultiSymbolMomentum") {
        MultiSymbolMomentumStrategy s;
        result = bt.run(s, feed);
    } else if (strategy == "PutCallParity") {
        PutCallParityStrategy s;
        result = bt.run(s, feed);
    } else if (strategy == "GoldenCross") {
        GoldenCrossStrategy s;
        result = bt.run(s, feed);
    } else if (strategy == "ShortStrangle") {
        ShortStrangleStrategy s;
        result = bt.run(s, feed);
    } else if (strategy == "IVFuturesSignal") {
        IVFuturesSignalStrategy s;
        result = bt.run(s, feed);
    } else {
        std::cerr << "Error: 알 수 없는 전략: " << strategy << "\n"
                  << "  사용 가능: FundingMomentum, MultiSymbolMomentum, PutCallParity, "
                     "GoldenCross, ShortStrangle, IVFuturesSignal\n";
        return 1;
    }

    result.print_summary();
    bt.save_results(result);
    return 0;
}
