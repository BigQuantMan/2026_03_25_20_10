#pragma once
#include "http_client.hpp"
#include "order_gateway.hpp"
#include "signer.hpp"
#include <memory>
#include <string>

class BinanceFuturesGateway : public IOrderGateway {
public:
    struct Config {
        std::string base_url = "https://demo-fapi.binance.com";
        std::string api_key;
        long        recv_window_ms = 5000;
        bool        use_test_endpoint = true;      // /fapi/v1/order/test
        bool        dry_run = true;                // true 면 네트워크 호출하지 않음
        bool        assume_filled_on_success = true; // replay/live bridge 용도
    };

    BinanceFuturesGateway(
        Config config,
        std::shared_ptr<IHttpClient> http_client,
        std::shared_ptr<ISigner> signer);

    ExecutionReport submit_order(const LiveOrderRequest& req) override;

private:
    Config                        config_;
    std::shared_ptr<IHttpClient>  http_client_;
    std::shared_ptr<ISigner>      signer_;

    static long long now_ms();
    static std::string format_decimal(double value);
    static std::string build_query_string(const LiveOrderRequest& req,
                                          long recv_window_ms,
                                          long long timestamp_ms);
    static std::string url_encode(const std::string& s);
    static std::string extract_order_id(const std::string& body);
};
