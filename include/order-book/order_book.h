//
// Created by Nainil Patel on 6/12/26.
//

#ifndef TRADING_INFRASTRUCTURE_ORDER_BOOK_H
#define TRADING_INFRASTRUCTURE_ORDER_BOOK_H

#include "common/spsc_ring.h"
#include "feed/parser.h"
#include <array>
#include <cstddef>
#include <cstdint>

using OrderBookRing = spsc_ring<MarketEvent, 1024>;
constexpr double tick_size = 0.1;
constexpr std::size_t book_depth = 50;

struct BookEvent {
    uint64_t sequence;
    std::chrono::system_clock::time_point exchange_ts{};
    std::chrono::steady_clock::time_point local_ts{};
    std::string symbol;
    std::string event_type;
    std::array<BookLevel, book_depth> top_bids;
    std::array<BookLevel, book_depth> top_asks;
    uint16_t bid_count{};
    uint16_t ask_count{};
};

class OrderBook {
public:
    BookEvent update_book(const MarketEvent& market_event);

private:
    std::array<double, 2'000'000> ask_order_book{};
    std::array<double, 2'000'000> bid_order_book{};
    std::array<uint64_t, 31'250> ask_bit_map{};
    std::array<uint64_t, 31'250> bid_bit_map{};

    static double index_to_price(std::size_t index);
    static std::size_t price_to_index(double price);
};

#endif