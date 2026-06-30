//
// Created by Nainil Patel on 6/12/26.
//

#include "order-book/order_book.h"
#include <iostream>
#include <optional>
#include <bit>

std::optional<BookEvent> OrderBook::update_book(const MarketEvent& market_event) {
    const std::vector<BookLevel>& asks = market_event.asks;
    const std::vector<BookLevel>& bids = market_event.bids;
    auto timestamp = market_event.timestamp;
    std::string type = market_event.type;
    std::string symbol = market_event.symbol;

    for (const BookLevel& ask : asks) {
        const size_t idx = price_to_index(ask.price);
        ask_order_book[idx] = ask.quantity;
        ask.quantity > 0 ? set_price_level_active(idx, false) : set_price_level_inactive(idx, false);
    }

    for (const BookLevel& bid : bids) {
        const size_t idx = price_to_index(bid.price);
        bid_order_book[idx] = bid.quantity;
        bid.quantity > 0 ? set_price_level_active(idx, true) : set_price_level_inactive(idx, true);
    }

    BookEvent book_event{};
    // book_event.sequence = market_event.sequence;
    // book_event.exchange_ts = market_event.timestamp;
    // book_event.local_ts = std::chrono::system_clock::now();
    // book_event.symbol = market_event.symbol;
    // book_event.event_type = market_event.type;
    // book_event.top_bids = top_bids;
    // book_event.top_asks = top_asks;
    book_event.top_ask = get_best_price(false);
    book_event.top_bid = get_best_price(true);

    return book_event;
}

double OrderBook::index_to_price(const std::size_t index) {
    return static_cast<double>(index) * tick_size;
}

std::size_t OrderBook::price_to_index(const double price) {
    return std::llround(price / tick_size);
}

void OrderBook::set_price_level_active(const size_t& idx, const bool& isBid) {
    const std::size_t index = idx / 64;
    const std::size_t bit = idx % 64;
    const uint64_t mask = 1ULL << bit;

    if (isBid) {
        const uint64_t current_value = bid_bit_map[index];
        const uint64_t newValue = current_value | mask;
        bid_bit_map[index] = newValue;
    } else {
        const uint64_t current_value = ask_bit_map[index];
        const uint64_t newValue = current_value | mask;
        ask_bit_map[index] = newValue;
    }
}

void OrderBook::set_price_level_inactive(const size_t& idx, const bool& isBid) {
    const std::size_t index = idx / 64;
    const std::size_t bit = idx % 64;
    const uint64_t mask = 1ULL << bit;

    if (isBid) {
        const uint64_t current_value = bid_bit_map[index];
        const uint64_t newValue = current_value & ~mask;
        bid_bit_map[index] = newValue;
    } else {
        const uint64_t current_value = ask_bit_map[index];
        const uint64_t newValue = current_value & ~mask;
        ask_bit_map[index] = newValue;
    }
}

double OrderBook::get_best_price(const bool& isBid) const {
    if (isBid) {
        for (std::size_t i = bid_bit_map.size(); i-- > 0;) {
            const uint64_t bits = bid_bit_map[i];

            if (bits != 0) {
                const std::size_t bit = 63 - std::countl_zero(bits);
                return index_to_price(i * 64 + bit);
            }
        }
    } else {
        for (std::size_t i = 0; i < ask_bit_map.size(); i++) {
            const uint64_t bits = ask_bit_map[i];

            if (bits != 0) {
                const std::size_t bit = std::countr_zero(bits);
                return index_to_price(i * 64 + bit);
            }
        }
    }

    return 0.0;
}