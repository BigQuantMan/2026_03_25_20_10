#include "../../include/live/live_trader.hpp"
#include "../../include/live/order_mapper.hpp"
#include <chrono>
#include <iostream>
#include <thread>

LiveTrader::LiveTrader(std::unique_ptr<Strategy> strategy,
                       IFeed& feed,
                       std::shared_ptr<IOrderGateway> gateway,
                       Config config)
    : strategy_(std::move(strategy))
    , feed_(feed)
    , gateway_(std::move(gateway))
    , config_(config) {}

void LiveTrader::log(const std::string& msg) const {
    if (config_.verbose) std::cerr << msg << "\n";
}

void LiveTrader::run() {
    feed_.reset();
    strategy_->on_start();

    int submitted_count = 0;
    int filled_count = 0;
    int accepted_count = 0;
    int processed_bars = 0;

    while (feed_.next()) {
        ++processed_bars;
        const MarketSnapshot& snap = feed_.current();
        auto orders = strategy_->on_bar(snap.timestamp(), snap);
        std::vector<Order> state_advance_orders;

        int seq = 0;
        for (auto& order : orders) {
            const double current_pos = local_positions_[order.symbol];
            OrderMapResult mapped = OrderMapper::map_future_order(order, current_pos);
            if (!mapped.supported) {
                log(std::string("[LiveTrader] skip: ") + mapped.reason);
                continue;
            }

            mapped.request.client_order_id = OrderMapper::make_client_order_id(order, snap.timestamp(), seq++);
            ExecutionReport rep = gateway_->submit_order(mapped.request);
            ++submitted_count;

            if (rep.accepted) {
                ++accepted_count;
                log(std::string("[LiveTrader] accepted: ") + mapped.request.symbol + " "
                    + mapped.request.side + " qty=" + std::to_string(mapped.request.quantity)
                    + (mapped.request.reduce_only ? " reduceOnly" : ""));
            } else {
                log(std::string("[LiveTrader] rejected: ") + rep.error_message + " response=" + rep.raw_response);
            }

            const bool advance_state = rep.accepted && (rep.filled || config_.advance_state_on_accept);
            if (advance_state) {
                local_positions_[order.symbol] += OrderMapper::signed_position_delta(order);
                state_advance_orders.push_back(order);
                if (rep.filled) {
                    ++filled_count;
                }
            }
        }

        if (!state_advance_orders.empty()) {
            strategy_->on_fill(state_advance_orders);
        }

        if (config_.max_bars > 0 && processed_bars >= config_.max_bars) {
            break;
        }

        if (config_.sleep_ms_between_bars > 0) {
            std::this_thread::sleep_for(std::chrono::milliseconds(config_.sleep_ms_between_bars));
        }
    }

    strategy_->on_end();
    log("[LiveTrader] done: bars=" + std::to_string(processed_bars)
        + " submitted=" + std::to_string(submitted_count)
        + " accepted=" + std::to_string(accepted_count)
        + " filled=" + std::to_string(filled_count));
}
