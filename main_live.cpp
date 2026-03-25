#include "include/futures_feed.hpp"
#include "include/live/binance_futures_gateway.hpp"
#include "include/live/curl_http_client.hpp"
#include "include/live/hmac_sha256_signer.hpp"
#include "include/live/live_trader.hpp"
#include "strategies/live_futures_strategies.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

static void print_live_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " --futures-csv <path> [options]\n\n"
        << "전략:\n"
        << "  --strategy LiveFundingMomentum | LiveGoldenCross\n\n"
        << "거래소 설정:\n"
        << "  --api-key <key>        (없으면 BINANCE_API_KEY 환경변수 사용)\n"
        << "  --api-secret <secret>  (없으면 BINANCE_API_SECRET 환경변수 사용)\n"
        << "  --base-url <url>       기본: https://demo-fapi.binance.com\n"
        << "  --send-live            실제 HTTP 호출 수행 (기본은 dry-run)\n"
        << "  --real-order           /fapi/v1/order 사용 (기본은 /fapi/v1/order/test)\n"
        << "  --no-assume-fill       성공 응답을 fill 로 간주하지 않음\n\n"
        << "피드/실행:\n"
        << "  --sleep-ms <n>         각 바 사이 대기 ms (기본 0)\n"
        << "  --quiet                로그 축소\n\n"
        << "전략 파라미터 (FundingMomentum):\n"
        << "  --symbol <sym>\n"
        << "  --quantity <qty>\n"
        << "  --long-threshold <x>\n"
        << "  --short-threshold <x>\n"
        << "  --exit-threshold <x>\n\n"
        << "전략 파라미터 (GoldenCross):\n"
        << "  --fast <n>\n"
        << "  --slow <n>\n"
        << "  --notional <usdt>\n\n"
        << "예시:\n"
        << "  " << prog << " --futures-csv results/prepared/futures_is_data.csv --strategy LiveFundingMomentum\n"
        << "  " << prog << " --futures-csv results/prepared/futures_is_data.csv --strategy LiveFundingMomentum --send-live\n";
}

int main(int argc, char* argv[]) {
    if (argc < 2) {
        print_live_usage(argv[0]);
        return 1;
    }

    std::string futures_csv;
    std::string strategy_name = "LiveFundingMomentum";
    std::string api_key = std::getenv("BINANCE_API_KEY") ? std::getenv("BINANCE_API_KEY") : "";
    std::string api_secret = std::getenv("BINANCE_API_SECRET") ? std::getenv("BINANCE_API_SECRET") : "";
    std::string base_url = "https://demo-fapi.binance.com";
    bool dry_run = true;
    bool use_test_endpoint = true;
    bool assume_fill = true;
    bool verbose = true;
    int sleep_ms = 0;

    // FundingMomentum defaults
    std::string symbol = "BTCUSDT";
    double quantity = 0.01;
    double long_threshold = -0.0001;
    double short_threshold =  0.0001;
    double exit_threshold =   0.00005;

    // GoldenCross defaults
    int fast = 5;
    int slow = 20;
    double notional = 100.0;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--futures-csv" && i + 1 < argc) futures_csv = argv[++i];
        else if (arg == "--strategy" && i + 1 < argc) strategy_name = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc) api_key = argv[++i];
        else if (arg == "--api-secret" && i + 1 < argc) api_secret = argv[++i];
        else if (arg == "--base-url" && i + 1 < argc) base_url = argv[++i];
        else if (arg == "--send-live") dry_run = false;
        else if (arg == "--real-order") use_test_endpoint = false;
        else if (arg == "--no-assume-fill") assume_fill = false;
        else if (arg == "--quiet") verbose = false;
        else if (arg == "--sleep-ms" && i + 1 < argc) sleep_ms = std::stoi(argv[++i]);
        else if (arg == "--symbol" && i + 1 < argc) symbol = argv[++i];
        else if (arg == "--quantity" && i + 1 < argc) quantity = std::stod(argv[++i]);
        else if (arg == "--long-threshold" && i + 1 < argc) long_threshold = std::stod(argv[++i]);
        else if (arg == "--short-threshold" && i + 1 < argc) short_threshold = std::stod(argv[++i]);
        else if (arg == "--exit-threshold" && i + 1 < argc) exit_threshold = std::stod(argv[++i]);
        else if (arg == "--fast" && i + 1 < argc) fast = std::stoi(argv[++i]);
        else if (arg == "--slow" && i + 1 < argc) slow = std::stoi(argv[++i]);
        else if (arg == "--notional" && i + 1 < argc) notional = std::stod(argv[++i]);
        else if (arg == "--help" || arg == "-h") {
            print_live_usage(argv[0]);
            return 0;
        }
    }

    if (futures_csv.empty()) {
        std::cerr << "Error: --futures-csv is required\n";
        return 1;
    }

    if (!dry_run && (api_key.empty() || api_secret.empty())) {
        std::cerr << "Error: live HTTP 호출에는 --api-key/--api-secret 또는 환경변수가 필요합니다.\n";
        return 1;
    }

    FuturesFeed feed(futures_csv, symbol);

    std::unique_ptr<Strategy> strategy;
    if (strategy_name == "LiveFundingMomentum") {
        auto s = std::make_unique<LiveFundingMomentumStrategy>();
        s->symbol = symbol;
        s->quantity = quantity;
        s->long_threshold = long_threshold;
        s->short_threshold = short_threshold;
        s->exit_threshold = exit_threshold;
        strategy = std::move(s);
    } else if (strategy_name == "LiveGoldenCross") {
        auto s = std::make_unique<LiveGoldenCrossStrategy>();
        s->fast_period = fast;
        s->slow_period = slow;
        s->notional_per_trade = notional;
        strategy = std::move(s);
    } else {
        std::cerr << "Error: unsupported live strategy: " << strategy_name << "\n";
        return 1;
    }

    auto http_client = std::make_shared<CurlHttpClient>();
    auto signer = std::make_shared<HmacSha256Signer>(api_secret);

    BinanceFuturesGateway::Config gw_cfg;
    gw_cfg.base_url = base_url;
    gw_cfg.api_key = api_key;
    gw_cfg.dry_run = dry_run;
    gw_cfg.use_test_endpoint = use_test_endpoint;
    gw_cfg.assume_filled_on_success = assume_fill;

    auto gateway = std::make_shared<BinanceFuturesGateway>(gw_cfg, http_client, signer);

    LiveTrader::Config trader_cfg;
    trader_cfg.verbose = verbose;
    trader_cfg.sleep_ms_between_bars = sleep_ms;

    LiveTrader trader(std::move(strategy), feed, gateway, trader_cfg);
    trader.run();
    return 0;
}
