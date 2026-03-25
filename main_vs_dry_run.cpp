#include "include/futures_feed.hpp"
#include "include/live/binance_futures_gateway.hpp"
#include "include/live/binance_rest_realtime_futures_feed.hpp"
#include "include/live/dummy_http_client.hpp"
#include "include/live/dummy_signer.hpp"
#include "include/live/live_trader.hpp"
#include "include/live/win_http_client.hpp"
#include "include/live/windows_hmac_sha256_signer.hpp"
#include "strategies/live_futures_strategies.hpp"

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>

static void print_usage(const char* prog) {
    std::cout
        << "Usage: " << prog << " [options]\n\n"
        << "This Windows/Visual Studio build supports both replay and realtime modes.\n\n"
        << "Modes:\n"
        << "  --mode replay|realtime      default: replay\n"
        << "  --send-test-order           send a real Binance Futures TEST order\n"
        << "  --advance-state-on-accept   advance strategy state after successful /order/test\n\n"
        << "Replay options:\n"
        << "  --futures-csv <path>        default: results\\prepared\\sample_futures_live.csv\n\n"
        << "Realtime options:\n"
        << "  --symbol <sym>              default: BTCUSDT\n"
        << "  --interval <iv>             default: 1m\n"
        << "  --poll-ms <n>               default: 1000\n"
        << "  --market-url <url>          default: https://fapi.binance.com\n\n"
        << "Order options:\n"
        << "  --order-url <url>           default: https://demo-fapi.binance.com\n"
        << "  --api-key <key>             or BINANCE_API_KEY env var\n"
        << "  --api-secret <secret>       or BINANCE_API_SECRET env var\n"
        << "  --recv-window <ms>          default: 5000\n\n"
        << "Strategy options:\n"
        << "  --strategy <name>           LiveFundingMomentum | LiveGoldenCross\n"
        << "  --quantity <qty>            FundingMomentum order quantity, default 0.01\n"
        << "  --long-threshold <x>        default -0.0001\n"
        << "  --short-threshold <x>       default +0.0001\n"
        << "  --exit-threshold <x>        default 0.00005\n"
        << "  --fast <n>                  GoldenCross fast MA, default 5\n"
        << "  --slow <n>                  GoldenCross slow MA, default 20\n"
        << "  --notional <usdt>           GoldenCross notional, default 100\n\n"
        << "Engine options:\n"
        << "  --sleep-ms <n>              replay delay between bars, default 0\n"
        << "  --max-bars <n>              stop after N bars, default 0 (unlimited)\n"
        << "  --quiet                     reduce logs\n";
}

int main(int argc, char* argv[]) {
    std::string mode = "replay";
    std::string futures_csv = "results/prepared/sample_futures_live.csv";
    std::string strategy_name = "LiveFundingMomentum";
    bool verbose = true;
    int sleep_ms = 0;
    int max_bars = 0;

    std::string symbol = "BTCUSDT";
    double quantity = 0.01;
    double long_threshold = -0.0001;
    double short_threshold = 0.0001;
    double exit_threshold = 0.00005;

    int fast = 5;
    int slow = 20;
    double notional = 100.0;

    std::string market_url = "https://fapi.binance.com";
    std::string order_url = "https://demo-fapi.binance.com";
    std::string interval = "1m";
    int poll_ms = 1000;
    std::string api_key = std::getenv("BINANCE_API_KEY") ? std::getenv("BINANCE_API_KEY") : "";
    std::string api_secret = std::getenv("BINANCE_API_SECRET") ? std::getenv("BINANCE_API_SECRET") : "";
    long recv_window_ms = 5000;
    bool send_test_order = false;
    bool advance_state_on_accept = false;

    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--mode" && i + 1 < argc) mode = argv[++i];
        else if (arg == "--futures-csv" && i + 1 < argc) futures_csv = argv[++i];
        else if (arg == "--strategy" && i + 1 < argc) strategy_name = argv[++i];
        else if (arg == "--symbol" && i + 1 < argc) symbol = argv[++i];
        else if (arg == "--quantity" && i + 1 < argc) quantity = std::stod(argv[++i]);
        else if (arg == "--long-threshold" && i + 1 < argc) long_threshold = std::stod(argv[++i]);
        else if (arg == "--short-threshold" && i + 1 < argc) short_threshold = std::stod(argv[++i]);
        else if (arg == "--exit-threshold" && i + 1 < argc) exit_threshold = std::stod(argv[++i]);
        else if (arg == "--fast" && i + 1 < argc) fast = std::stoi(argv[++i]);
        else if (arg == "--slow" && i + 1 < argc) slow = std::stoi(argv[++i]);
        else if (arg == "--notional" && i + 1 < argc) notional = std::stod(argv[++i]);
        else if (arg == "--sleep-ms" && i + 1 < argc) sleep_ms = std::stoi(argv[++i]);
        else if (arg == "--max-bars" && i + 1 < argc) max_bars = std::stoi(argv[++i]);
        else if (arg == "--interval" && i + 1 < argc) interval = argv[++i];
        else if (arg == "--poll-ms" && i + 1 < argc) poll_ms = std::stoi(argv[++i]);
        else if (arg == "--market-url" && i + 1 < argc) market_url = argv[++i];
        else if (arg == "--order-url" && i + 1 < argc) order_url = argv[++i];
        else if (arg == "--api-key" && i + 1 < argc) api_key = argv[++i];
        else if (arg == "--api-secret" && i + 1 < argc) api_secret = argv[++i];
        else if (arg == "--recv-window" && i + 1 < argc) recv_window_ms = std::stol(argv[++i]);
        else if (arg == "--send-test-order") send_test_order = true;
        else if (arg == "--advance-state-on-accept") advance_state_on_accept = true;
        else if (arg == "--quiet") verbose = false;
        else if (arg == "--help" || arg == "-h") {
            print_usage(argv[0]);
            return 0;
        }
    }

    try {
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
            std::cerr << "Error: unsupported strategy: " << strategy_name << "\n";
            print_usage(argv[0]);
            return 1;
        }

        std::shared_ptr<IHttpClient> http_client;
        std::shared_ptr<ISigner> signer;
        if (send_test_order || mode == "realtime") {
            http_client = std::make_shared<WinHttpClient>();
        } else {
            http_client = std::make_shared<DummyHttpClient>();
        }
        if (send_test_order) {
            if (api_key.empty() || api_secret.empty()) {
                std::cerr << "Error: api key/secret required for --send-test-order\n";
                return 1;
            }
            signer = std::make_shared<WindowsHmacSha256Signer>(api_secret);
        } else {
            signer = std::make_shared<DummySigner>();
        }

        BinanceFuturesGateway::Config gw_cfg;
        gw_cfg.base_url = order_url;
        gw_cfg.api_key = api_key;
        gw_cfg.recv_window_ms = recv_window_ms;
        gw_cfg.dry_run = !send_test_order;
        gw_cfg.use_test_endpoint = true;
        gw_cfg.assume_filled_on_success = false;
        auto gateway = std::make_shared<BinanceFuturesGateway>(gw_cfg, http_client, signer);

        LiveTrader::Config trader_cfg;
        trader_cfg.verbose = verbose;
        trader_cfg.sleep_ms_between_bars = sleep_ms;
        trader_cfg.max_bars = max_bars;
        trader_cfg.advance_state_on_accept = advance_state_on_accept;

        std::cerr << "[trackA_live_vs] mode=" << mode
                  << " strategy=" << strategy_name
                  << " symbol=" << symbol << "\n";
        if (send_test_order) {
            std::cerr << "[trackA_live_vs] sending Binance Futures TEST orders to " << order_url << "\n";
            if (advance_state_on_accept) {
                std::cerr << "[trackA_live_vs] NOTE: strategy state will advance on accepted /order/test responses.\n";
            }
        } else {
            std::cerr << "[trackA_live_vs] dry-run order mode\n";
        }

        if (mode == "realtime") {
            BinanceRestPollingFuturesFeed::Config feed_cfg;
            feed_cfg.base_url = market_url;
            feed_cfg.symbol = symbol;
            feed_cfg.interval = interval;
            feed_cfg.poll_ms = poll_ms;
            feed_cfg.include_funding_rate = (strategy_name == "LiveFundingMomentum");
            feed_cfg.verbose = verbose;

            BinanceRestPollingFuturesFeed realtime_feed(feed_cfg, http_client);
            LiveTrader trader(std::move(strategy), realtime_feed, gateway, trader_cfg);
            trader.run();
        } else {
            FuturesFeed replay_feed(futures_csv, symbol);
            LiveTrader trader(std::move(strategy), replay_feed, gateway, trader_cfg);
            trader.run();
        }
        return 0;
    } catch (const std::exception& e) {
        std::cerr << "Fatal error: " << e.what() << "\n";
        return 1;
    }
}
