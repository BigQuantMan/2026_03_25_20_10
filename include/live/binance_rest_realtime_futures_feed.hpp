#pragma once
#include "../feed_base.hpp"
#include "http_client.hpp"
#include <cstdint>
#include <memory>
#include <string>

class BinanceRestPollingFuturesFeed : public IFeed {
public:
    struct Config {
        std::string base_url = "https://fapi.binance.com";
        std::string symbol = "BTCUSDT";
        std::string interval = "1m";
        int poll_ms = 1000;
        bool include_funding_rate = true;
        bool verbose = true;
    };

    BinanceRestPollingFuturesFeed(Config config, std::shared_ptr<IHttpClient> http_client);

    void reset() override;
    bool next() override;
    const MarketSnapshot& current() const override;
    size_t remaining() const override;
    size_t total() const override;
    int64_t start_ts() const override;
    int64_t end_ts() const override;

public:
    struct KlineBar {
        int64_t open_time = 0;
        int64_t close_time = 0;
        double open = 0.0;
        double high = 0.0;
        double low = 0.0;
        double close = 0.0;
        double volume = 0.0;
        double quote_volume = 0.0;
    };

private:
    Config config_;
    std::shared_ptr<IHttpClient> http_client_;
    MarketSnapshot current_;
    int64_t last_emitted_open_time_ = 0;
    bool started_ = false;

    void log(const std::string& msg) const;
    bool fetch_latest_closed_bar(KlineBar& out_bar, double& out_funding_rate, std::string& error_message) const;
};
