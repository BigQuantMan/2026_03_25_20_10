#pragma once
#include "../feed_base.hpp"
#include "../strategy.hpp"
#include "order_gateway.hpp"
#include <memory>
#include <string>
#include <unordered_map>

class LiveTrader {
public:
    struct Config {
        bool verbose = true;
        int  sleep_ms_between_bars = 0;
        bool advance_state_on_accept = false;
        int  max_bars = 0; // 0 = unlimited
    };

    LiveTrader(std::unique_ptr<Strategy> strategy,
               IFeed&                     feed,
               std::shared_ptr<IOrderGateway> gateway,
               Config config);

    void run();

private:
    std::unique_ptr<Strategy>       strategy_;
    IFeed&                          feed_;
    std::shared_ptr<IOrderGateway>  gateway_;
    Config                          config_;
    std::unordered_map<std::string, double> local_positions_;

    void log(const std::string& msg) const;
};
