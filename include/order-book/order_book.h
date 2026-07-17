//
// Created by Nainil Patel on 6/12/26.
//

#ifndef TRADING_INFRASTRUCTURE_ORDER_BOOK_H
#define TRADING_INFRASTRUCTURE_ORDER_BOOK_H

#include "templates/spsc_ring.h"
#include "feed/parser.h"
#include <array>
#include <cstddef>
#include <cstdint>

using OrderBookRing = spsc_ring<MarketEvent, 1024>;
constexpr double tick_size = 0.1;

class OrderBook {
public:
    TelemetryData telemetry_data;
    bool update_book(const MarketEvent& market_event);
    std::array<BookLevel, N> get_top_n_levels(const bool &isBid) const;

private:
    std::array<double, 2'000'000> ask_order_book{};
    std::array<double, 2'000'000> bid_order_book{};
    std::array<uint64_t, 31'250> ask_bit_map{};
    std::array<uint64_t, 31'250> bid_bit_map{};
    std::array<BookLevel, N> top_n_asks{};
    std::array<BookLevel, N> top_n_bids{};
    BookLevel top_ask{};
    BookLevel top_bid{};

    static double index_to_price(std::size_t index);
    static std::size_t price_to_index(double price);
    void reset_book();
    void set_top_n_prices();
    void set_book_telemetry();
    void set_price_level_active(const size_t& idx, const bool& isBid);
    void set_price_level_inactive(const size_t& idx, const bool& isBid);
};

#endif
