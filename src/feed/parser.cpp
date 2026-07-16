//
// Created by Nainil Patel on 6/19/26.
//

#include "feed/parser.h"
#include "common/time_utils.h"
#include <simdjson.h>
#include <cstdint>

std::optional<MarketEvent> parse_kraken_book_event(const ExchangeMessage& event) {
    MarketEvent out{};
    out.telemetry_data = event.telemetry_data;

    simdjson::ondemand::parser parser;
    simdjson::padded_string json{event.data};

    auto doc = parser.iterate(json);
    if (doc.error()) return std::nullopt;

    auto channel = doc["channel"].get_string();
    if (channel.error() || channel.value() != "book") return std::nullopt;

    auto type = doc["type"].get_string();
    if (type.error()) return std::nullopt;
    out.type = type.value();

    auto data_arr = doc["data"].get_array();
    if (data_arr.error()) return std::nullopt;

    for (auto item : data_arr.value()) {
        auto symbol = item["symbol"].get_string();
        if (symbol.error()) return std::nullopt;
        out.telemetry_data.identifier.symbol = std::string(symbol.value());

        auto bids = item["bids"].get_array();
        if (!bids.error()) {
            for (auto bid : bids.value()) {
                auto price = bid["price"].get_double();
                auto quantity = bid["qty"].get_double();
                if (price.error() || quantity.error()) return std::nullopt;
                out.bids.push_back(BookLevel{price.value(), quantity.value()});
            }
        }

        auto asks = item["asks"].get_array();
        if (!asks.error()) {
            for (auto ask : asks.value()) {
                auto price = ask["price"].get_double();
                auto quantity = ask["qty"].get_double();
                if (price.error() || quantity.error()) return std::nullopt;
                out.asks.push_back(BookLevel{price.value(), quantity.value()});
                }
        }

        auto timestamp = item["timestamp"].get_string();
        if (timestamp.error()) return std::nullopt;
        out.telemetry_data.latency_metrics.exchange_ts = parse_iso_utc_timestamp(timestamp.value());
    }

    out.telemetry_data.latency_metrics.parse_complete_ts = std::chrono::steady_clock::now();
    return out;
}