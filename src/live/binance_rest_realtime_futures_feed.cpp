#include "../../include/live/binance_rest_realtime_futures_feed.hpp"
#include <chrono>
#include <cctype>
#include <iostream>
#include <sstream>
#include <thread>
#include <vector>

namespace {

bool parse_double(const std::string& s, double& out) {
    try {
        out = std::stod(s);
        return true;
    } catch (...) {
        return false;
    }
}

bool parse_int64(const std::string& s, int64_t& out) {
    try {
        out = std::stoll(s);
        return true;
    } catch (...) {
        return false;
    }
}

std::vector<std::string> parse_json_array_items(const std::string& json_array_text) {
    std::vector<std::string> items;
    std::string cur;
    bool in_string = false;
    bool escape = false;

    for (char ch : json_array_text) {
        if (!in_string) {
            if (ch == '[' || ch == ']' || std::isspace(static_cast<unsigned char>(ch))) {
                continue;
            }
            if (ch == ',') {
                if (!cur.empty()) {
                    items.push_back(cur);
                    cur.clear();
                }
                continue;
            }
            if (ch == '"') {
                in_string = true;
                continue;
            }
            cur.push_back(ch);
        } else {
            if (escape) {
                cur.push_back(ch);
                escape = false;
            } else if (ch == '\\') {
                escape = true;
            } else if (ch == '"') {
                in_string = false;
            } else {
                cur.push_back(ch);
            }
        }
    }
    if (!cur.empty()) {
        items.push_back(cur);
    }
    return items;
}

std::vector<std::string> extract_nested_arrays(const std::string& json) {
    std::vector<std::string> arrays;
    int depth = 0;
    size_t start = std::string::npos;
    for (size_t i = 0; i < json.size(); ++i) {
        const char ch = json[i];
        if (ch == '[') {
            ++depth;
            if (depth == 2) {
                start = i;
            }
        } else if (ch == ']') {
            if (depth == 2 && start != std::string::npos) {
                arrays.push_back(json.substr(start, i - start + 1));
                start = std::string::npos;
            }
            --depth;
        }
    }
    return arrays;
}

bool parse_kline_from_array_text(const std::string& text,
                                 BinanceRestPollingFuturesFeed::KlineBar& out_bar) {
    auto items = parse_json_array_items(text);
    if (items.size() < 8) {
        return false;
    }

    return parse_int64(items[0], out_bar.open_time)
        && parse_double(items[1], out_bar.open)
        && parse_double(items[2], out_bar.high)
        && parse_double(items[3], out_bar.low)
        && parse_double(items[4], out_bar.close)
        && parse_double(items[5], out_bar.volume)
        && parse_int64(items[6], out_bar.close_time)
        && parse_double(items[7], out_bar.quote_volume);
}

bool extract_json_string_field(const std::string& body, const std::string& key, std::string& out) {
    const std::string needle = "\"" + key + "\":";
    auto pos = body.find(needle);
    if (pos == std::string::npos) return false;
    pos += needle.size();
    while (pos < body.size() && std::isspace(static_cast<unsigned char>(body[pos]))) ++pos;
    if (pos >= body.size()) return false;

    if (body[pos] == '"') {
        ++pos;
        auto end = body.find('"', pos);
        if (end == std::string::npos) return false;
        out = body.substr(pos, end - pos);
        return true;
    }

    auto end = pos;
    while (end < body.size() && body[end] != ',' && body[end] != '}' && !std::isspace(static_cast<unsigned char>(body[end]))) ++end;
    out = body.substr(pos, end - pos);
    return true;
}

} // namespace

BinanceRestPollingFuturesFeed::BinanceRestPollingFuturesFeed(Config config, std::shared_ptr<IHttpClient> http_client)
    : config_(std::move(config))
    , http_client_(std::move(http_client)) {}

void BinanceRestPollingFuturesFeed::log(const std::string& msg) const {
    if (config_.verbose) {
        std::cerr << msg << "\n";
    }
}

void BinanceRestPollingFuturesFeed::reset() {
    current_ = MarketSnapshot{};
    last_emitted_open_time_ = 0;
    started_ = false;
}

const MarketSnapshot& BinanceRestPollingFuturesFeed::current() const {
    return current_;
}

size_t BinanceRestPollingFuturesFeed::remaining() const { return 0; }
size_t BinanceRestPollingFuturesFeed::total() const { return 0; }
int64_t BinanceRestPollingFuturesFeed::start_ts() const { return 0; }
int64_t BinanceRestPollingFuturesFeed::end_ts() const { return 0; }

bool BinanceRestPollingFuturesFeed::fetch_latest_closed_bar(KlineBar& out_bar,
                                                            double& out_funding_rate,
                                                            std::string& error_message) const {
    HttpRequest kline_req;
    kline_req.method = "GET";
    kline_req.url = config_.base_url + "/fapi/v1/klines?symbol=" + config_.symbol
        + "&interval=" + config_.interval + "&limit=2";

    HttpResponse kline_resp = http_client_->send(kline_req);
    if (kline_resp.status_code < 200 || kline_resp.status_code >= 300) {
        error_message = "kline request failed: status=" + std::to_string(kline_resp.status_code)
            + " msg=" + kline_resp.error_message + " body=" + kline_resp.body;
        return false;
    }

    auto arrays = extract_nested_arrays(kline_resp.body);
    if (arrays.empty()) {
        error_message = "kline parse failed: no nested arrays";
        return false;
    }

    const size_t idx = (arrays.size() >= 2) ? arrays.size() - 2 : arrays.size() - 1;
    if (!parse_kline_from_array_text(arrays[idx], out_bar)) {
        error_message = "kline parse failed: malformed kline array";
        return false;
    }

    out_funding_rate = 0.0;
    if (!config_.include_funding_rate) {
        return true;
    }

    HttpRequest funding_req;
    funding_req.method = "GET";
    funding_req.url = config_.base_url + "/fapi/v1/premiumIndex?symbol=" + config_.symbol;
    HttpResponse funding_resp = http_client_->send(funding_req);
    if (funding_resp.status_code < 200 || funding_resp.status_code >= 300) {
        error_message = "premiumIndex request failed: status=" + std::to_string(funding_resp.status_code)
            + " msg=" + funding_resp.error_message + " body=" + funding_resp.body;
        return false;
    }

    std::string fr_str;
    if (!extract_json_string_field(funding_resp.body, "lastFundingRate", fr_str)) {
        error_message = "premiumIndex parse failed: lastFundingRate missing";
        return false;
    }
    if (!parse_double(fr_str, out_funding_rate)) {
        error_message = "premiumIndex parse failed: invalid funding rate";
        return false;
    }
    return true;
}

bool BinanceRestPollingFuturesFeed::next() {
    using namespace std::chrono;
    while (true) {
        KlineBar bar;
        double funding_rate = 0.0;
        std::string err;
        if (fetch_latest_closed_bar(bar, funding_rate, err)) {
            if (bar.open_time > last_emitted_open_time_) {
                FuturesBar f{};
                f.symbol = config_.symbol;
                f.timestamp = bar.open_time;
                f.open = bar.open;
                f.high = bar.high;
                f.low = bar.low;
                f.close = bar.close;
                f.volume = bar.volume;
                f.quote_volume = bar.quote_volume;
                f.funding_rate = funding_rate;
                f.open_interest = 0.0;
                f.long_ratio = 0.0;
                f.short_ratio = 0.0;
                f.taker_buy_ratio = 0.0;
                f.is_active = true;

                std::unordered_map<std::string, FuturesBar> mp;
                mp.emplace(config_.symbol, f);
                current_ = MarketSnapshot::make_futures_snapshot(bar.open_time, bar.close, std::move(mp));
                last_emitted_open_time_ = bar.open_time;
                started_ = true;
                log("[BinanceRestPollingFuturesFeed] closed bar: symbol=" + config_.symbol
                    + " ts=" + std::to_string(bar.open_time)
                    + " close=" + std::to_string(bar.close)
                    + " volume=" + std::to_string(bar.volume)
                    + " funding=" + std::to_string(funding_rate));
                return true;
            }
        } else {
            log(std::string("[BinanceRestPollingFuturesFeed] poll failed: ") + err);
        }

        std::this_thread::sleep_for(milliseconds(config_.poll_ms));
    }
}
