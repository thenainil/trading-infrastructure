//
// Created by Nainil Patel on 6/21/26.
//

#ifndef TRADING_INFRASTRUCTURE_METRICS_H
#define TRADING_INFRASTRUCTURE_METRICS_H

#include <array>
#include <chrono>
#include <cstdint>
#include <string>
#include "templates/spsc_ring.h"

constexpr std::size_t N = 10;

struct Identifier {
    uint64_t monotonic_id{}; // Feed
    std::string symbol{}; // Parser
};

struct LatencyMetrics {
    std::chrono::system_clock::time_point exchange_ts{}; // Parser
    std::chrono::system_clock::time_point message_received_wall_ts{}; // Feed
    std::chrono::steady_clock::time_point message_received_ts{}; // Feed
    std::chrono::steady_clock::time_point parse_complete_ts{}; // Parser
    std::chrono::steady_clock::time_point order_book_complete_ts{}; // Order Book
    std::chrono::steady_clock::time_point feature_calculation_complete_ts{}; // Features
    std::chrono::steady_clock::time_point strategy_decision_complete_ts{}; // Strategy
};

struct BookLevelTelemetry {
    double price{};
    double quantity{};
};

struct BookTelemetry {
    std::array<BookLevelTelemetry, N> bids{};
    std::array<BookLevelTelemetry, N> asks{};
};

struct FeaturesTelemetry {
    double top_bid{};
    double top_ask{};
    double top_bid_qty{};
    double top_ask_qty{};
    double mid_price{};
    double microprice{};
    double microprice_edge_tick{};
    double spread{};
    double tick_spread{};
    double top_level_imbalance{};
    double ofi{};
    double top_n_imbalance{};
    double top_n_ofi{};
};

struct StrategyTelemetry {
    std::string decision;
};

struct TelemetryData {
    LatencyMetrics latency_metrics{};
    Identifier identifier{};
    BookTelemetry book_telemetry{};
    FeaturesTelemetry features_telemetry{};
    StrategyTelemetry strategy_telemetry{};
};

using TelemetrySpscRing = spsc_ring<TelemetryData, 1024>;
std::string serialize_metadata_to_json(const TelemetryData& metadata);

#endif //TRADING_INFRASTRUCTURE_METRICS_H
