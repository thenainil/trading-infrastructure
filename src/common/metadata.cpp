//
// Created by Nainil Patel on 6/21/26.
//

#include "common/metadata.h"

#include <sstream>

namespace {
    long long duration_ns(
        const std::chrono::steady_clock::time_point start,
        const std::chrono::steady_clock::time_point end) {
        return std::chrono::duration_cast<std::chrono::nanoseconds>(end - start).count();
    }

    long long duration_us(
        const std::chrono::system_clock::time_point start,
        const std::chrono::system_clock::time_point end) {
        return std::chrono::duration_cast<std::chrono::microseconds>(end - start).count();
    }
}

std::string serialize_metadata_to_json(const Metadata& metadata) {
    std::ostringstream out;

    out << '{'
        << "\"monotonic_id\":" << metadata.monotonic_id << ','
        << "\"symbol\":\"" << metadata.symbol << "\","
        << "\"network_latency_us\":" << duration_us(metadata.exchange_ts, metadata.message_received_wall_ts) << ','
        << "\"parse_latency_ns\":" << duration_ns(metadata.message_received_ts, metadata.parse_complete_ts) << ','
        << "\"order_book_latency_ns\":" << duration_ns(metadata.parse_complete_ts, metadata.order_book_complete_ts) << ','
        << "\"feature_latency_ns\":" << duration_ns(metadata.order_book_complete_ts, metadata.feature_calculation_complete_ts) << ','
        << "\"strategy_latency_ns\":" << duration_ns(metadata.feature_calculation_complete_ts, metadata.strategy_decision_complete_ts) << ','
        << "\"total_local_latency_ns\":" << duration_ns(metadata.message_received_ts, metadata.strategy_decision_complete_ts)
        << '}';

    return out.str();
}
