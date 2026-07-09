//
// Created by Nainil Patel on 6/21/26.
//

#ifndef TRADING_INFRASTRUCTURE_METRICS_H
#define TRADING_INFRASTRUCTURE_METRICS_H

#include <chrono>
#include <cstdint>
#include <string>

#include "templates/spsc_ring.h"

struct Metadata {
    std::chrono::system_clock::time_point exchange_ts{}; // Parser
    std::chrono::system_clock::time_point message_received_wall_ts{}; // Feed
    std::chrono::steady_clock::time_point message_received_ts{}; // Feed
    std::chrono::steady_clock::time_point parse_complete_ts{}; // Parser
    std::chrono::steady_clock::time_point order_book_complete_ts{}; // Order Book
    std::chrono::steady_clock::time_point feature_calculation_complete_ts{}; // Features
    std::chrono::steady_clock::time_point strategy_decision_complete_ts{}; // Strategy

    uint64_t monotonic_id{}; // Feed
    std::string symbol{}; // Parser
};

using MetadataSpscRing = spsc_ring<Metadata, 1024>;

std::string serialize_metadata_to_json(const Metadata& metadata);

#endif //TRADING_INFRASTRUCTURE_METRICS_H
