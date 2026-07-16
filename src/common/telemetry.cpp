//
// Created by Nainil Patel on 6/21/26.
//

#include "common/telemetry.h"

#include <sstream>
#include <string_view>

namespace {
    std::string escape_json_string(const std::string_view value) {
        std::ostringstream out;

        for (const char c : value) {
            switch (c) {
                case '"': out << "\\\""; break;
                case '\\': out << "\\\\"; break;
                case '\b': out << "\\b"; break;
                case '\f': out << "\\f"; break;
                case '\n': out << "\\n"; break;
                case '\r': out << "\\r"; break;
                case '\t': out << "\\t"; break;
                default:
                    const auto byte = static_cast<unsigned char>(c);
                    if (byte < 0x20) {
                        constexpr char hex[] = "0123456789abcdef";
                        out << "\\u00" << hex[(byte >> 4) & 0x0f] << hex[byte & 0x0f];
                    } else {
                        out << c;
                    }
            }
        }

        return out.str();
    }

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

    void write_book_levels(std::ostringstream& out, const std::array<BookLevelTelemetry, N>& levels) {
        out << '[';
        for (std::size_t i = 0; i < levels.size(); ++i) {
            if (i > 0) out << ',';
            out << '{'
                << "\"price\":" << levels[i].price << ','
                << "\"quantity\":" << levels[i].quantity
                << '}';
        }
        out << ']';
    }
}

std::string serialize_metadata_to_json(const TelemetryData& telemetry_data) {
    std::ostringstream out;

    out << '{'
        << "\"identifier\":{"
        << "\"monotonic_id\":" << telemetry_data.identifier.monotonic_id << ','
        << "\"symbol\":\"" << escape_json_string(telemetry_data.identifier.symbol) << "\""
        << "},"
        << "\"latency_metrics\":{"
        << "\"network_latency_us\":" << duration_us(telemetry_data.latency_metrics.exchange_ts,
            telemetry_data.latency_metrics.message_received_wall_ts) << ','
        << "\"parse_latency_ns\":" << duration_ns(telemetry_data.latency_metrics.message_received_ts,
            telemetry_data.latency_metrics.parse_complete_ts) << ','
        << "\"order_book_latency_ns\":" << duration_ns(telemetry_data.latency_metrics.parse_complete_ts,
            telemetry_data.latency_metrics.order_book_complete_ts) << ','
        << "\"feature_latency_ns\":" << duration_ns(telemetry_data.latency_metrics.order_book_complete_ts,
            telemetry_data.latency_metrics.feature_calculation_complete_ts) << ','
        << "\"strategy_latency_ns\":" << duration_ns(telemetry_data.latency_metrics.feature_calculation_complete_ts,
            telemetry_data.latency_metrics.strategy_decision_complete_ts) << ','
        << "\"total_local_latency_ns\":" << duration_ns(telemetry_data.latency_metrics.message_received_ts,
            telemetry_data.latency_metrics.strategy_decision_complete_ts)
        << "},"
        << "\"book_telemetry\":{"
        << "\"bids\":";
    write_book_levels(out, telemetry_data.book_telemetry.bids);
    out << ",\"asks\":";
    write_book_levels(out, telemetry_data.book_telemetry.asks);
    out << "},"
        << "\"features_telemetry\":{"
        << "\"top_bid\":" << telemetry_data.features_telemetry.top_bid << ','
        << "\"top_ask\":" << telemetry_data.features_telemetry.top_ask << ','
        << "\"top_bid_qty\":" << telemetry_data.features_telemetry.top_bid_qty << ','
        << "\"top_ask_qty\":" << telemetry_data.features_telemetry.top_ask_qty << ','
        << "\"mid_price\":" << telemetry_data.features_telemetry.mid_price << ','
        << "\"microprice\":" << telemetry_data.features_telemetry.microprice << ','
        << "\"microprice_edge_tick\":" << telemetry_data.features_telemetry.microprice_edge_tick << ','
        << "\"spread\":" << telemetry_data.features_telemetry.spread << ','
        << "\"tick_spread\":" << telemetry_data.features_telemetry.tick_spread << ','
        << "\"top_level_imbalance\":" << telemetry_data.features_telemetry.top_level_imbalance << ','
        << "\"ofi\":" << telemetry_data.features_telemetry.ofi << ','
        << "\"top_n_imbalance\":" << telemetry_data.features_telemetry.top_n_imbalance << ','
        << "\"top_n_ofi\":" << telemetry_data.features_telemetry.top_n_ofi
        << "},"
        << "\"strategy_telemetry\":{"
        << "\"decision\":\"" << escape_json_string(telemetry_data.strategy_telemetry.decision) << "\""
        << "}"
        << '}';

    return out.str();
}
