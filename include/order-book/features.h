//
// Created by Nainil Patel on 6/23/26.
//

#ifndef TRADING_INFRASTRUCTURE_FEATURES_H
#define TRADING_INFRASTRUCTURE_FEATURES_H
#include "order_book.h"

struct FeatureBook {
    TelemetryData telemetry_data;

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

using FeatureBookRing = spsc_ring<FeatureBook, 1024>;

class Features {

    std::array<BookLevel, N> previous_top_bids{};
    std::array<BookLevel, N> previous_top_asks{};
    bool previous_book_exists{};

public:
    std::optional<FeatureBook> calculate_features(const OrderBook& order_book);
};

#endif //TRADING_INFRASTRUCTURE_FEATURES_H