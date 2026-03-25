#include "../../include/live/binance_futures_gateway.hpp"
#include <chrono>
#include <cctype>
#include <iomanip>
#include <map>
#include <sstream>

BinanceFuturesGateway::BinanceFuturesGateway(
    Config config,
    std::shared_ptr<IHttpClient> http_client,
    std::shared_ptr<ISigner> signer)
    : config_(std::move(config))
    , http_client_(std::move(http_client))
    , signer_(std::move(signer)) {}

ExecutionReport BinanceFuturesGateway::submit_order(const LiveOrderRequest& req) {
    ExecutionReport rep;
    rep.client_order_id = req.client_order_id;

    const long long ts = now_ms();
    const std::string payload = build_query_string(req, config_.recv_window_ms, ts);
    const std::string signature = signer_->sign(payload);

    if (config_.dry_run) {
        rep.accepted = true;
        rep.filled   = config_.assume_filled_on_success;
        rep.http_status = 200;
        rep.raw_response = std::string("DRY_RUN payload=") + payload + "&signature=" + signature;
        return rep;
    }

    HttpRequest http_req;
    http_req.method = "POST";
    http_req.url = config_.base_url + (config_.use_test_endpoint ? "/fapi/v1/order/test" : "/fapi/v1/order");
    http_req.body = payload + "&signature=" + url_encode(signature);
    http_req.headers.push_back({"X-MBX-APIKEY", config_.api_key});
    http_req.headers.push_back({"Content-Type", "application/x-www-form-urlencoded"});

    HttpResponse http_resp = http_client_->send(http_req);
    rep.http_status = http_resp.status_code;
    rep.raw_response = http_resp.body.empty() ? http_resp.error_message : http_resp.body;
    rep.accepted = (http_resp.status_code >= 200 && http_resp.status_code < 300);
    rep.filled = rep.accepted && config_.assume_filled_on_success;
    rep.venue_order_id = extract_order_id(http_resp.body);
    if (!rep.accepted) {
        rep.error_message = http_resp.error_message.empty() ? "Binance Futures order rejected" : http_resp.error_message;
    }
    return rep;
}

long long BinanceFuturesGateway::now_ms() {
    using namespace std::chrono;
    return duration_cast<milliseconds>(system_clock::now().time_since_epoch()).count();
}

std::string BinanceFuturesGateway::format_decimal(double value) {
    std::ostringstream oss;
    oss << std::fixed << std::setprecision(8) << value;
    std::string s = oss.str();
    while (!s.empty() && s.back() == '0') s.pop_back();
    if (!s.empty() && s.back() == '.') s.pop_back();
    return s;
}

std::string BinanceFuturesGateway::build_query_string(const LiveOrderRequest& req,
                                                      long recv_window_ms,
                                                      long long timestamp_ms) {
    std::map<std::string, std::string> params;
    params["symbol"] = req.symbol;
    params["side"] = req.side;
    params["type"] = req.type;
    params["quantity"] = format_decimal(req.quantity);
    params["timestamp"] = std::to_string(timestamp_ms);
    params["recvWindow"] = std::to_string(recv_window_ms);
    if (req.reduce_only) params["reduceOnly"] = "true";
    if (!req.client_order_id.empty()) params["newClientOrderId"] = req.client_order_id;

    std::ostringstream oss;
    bool first = true;
    for (const auto& [k, v] : params) {
        if (!first) oss << '&';
        first = false;
        oss << url_encode(k) << '=' << url_encode(v);
    }
    return oss.str();
}

std::string BinanceFuturesGateway::url_encode(const std::string& s) {
    std::ostringstream escaped;
    escaped.fill('0');
    escaped << std::hex;
    for (unsigned char c : s) {
        if (std::isalnum(c) || c == '-' || c == '_' || c == '.' || c == '~') {
            escaped << c;
        } else {
            escaped << '%' << std::uppercase << std::setw(2) << int(c) << std::nouppercase;
        }
    }
    return escaped.str();
}

std::string BinanceFuturesGateway::extract_order_id(const std::string& body) {
    const std::string key = "\"orderId\":";
    auto pos = body.find(key);
    if (pos == std::string::npos) return {};
    pos += key.size();
    auto end = pos;
    while (end < body.size() && std::isdigit(static_cast<unsigned char>(body[end]))) ++end;
    return body.substr(pos, end - pos);
}
