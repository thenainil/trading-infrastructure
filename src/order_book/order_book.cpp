//
// Created by Nainil Patel on 6/12/26.
//

#include "order-book/order_book.h"

#include <iostream>

bool OrderBook::update_book(const MarketEvent& market_event) {
    telemetry_data = market_event.telemetry_data;

    if (market_event.type == "snapshot" || market_event.type == "SNAPSHOT") {
        reset_book();
    }

    const std::vector<BookLevel>& asks = market_event.asks;
    const std::vector<BookLevel>& bids = market_event.bids;

    for (const BookLevel& ask : asks) {
        const size_t idx = price_to_index(ask.price);
        ask_order_book[idx] = ask.quantity;

        if (ask.quantity > 0) {
            set_price_level_active(idx, false);

            if (top_ask.price == 0.0 || ask.price < top_ask.price) {
                top_ask.price = ask.price;
            }
        } else {
            set_price_level_inactive(idx, false);
        }
    }

    for (const BookLevel& bid : bids) {
        const size_t idx = price_to_index(bid.price);
        bid_order_book[idx] = bid.quantity;

        if (bid.quantity > 0) {
            set_price_level_active(idx, true);

            if (top_bid.price == 0.0 || bid.price > top_bid.price) {
                top_bid.price = bid.price;
            }
        } else {
            set_price_level_inactive(idx, true);
        }
    }

    set_top_n_prices();
    set_book_telemetry();
    telemetry_data.latency_metrics.order_book_complete_ts = std::chrono::steady_clock::now();
    return true;
}

std::array<BookLevel, N> OrderBook::get_top_n_levels(const bool &isBid) const {
    if (isBid) {
        return top_n_bids;
    }
    return top_n_asks;
}

double OrderBook::index_to_price(const std::size_t index) {
    return static_cast<double>(index) * tick_size;
}

std::size_t OrderBook::price_to_index(const double price) {
    return std::llround(price / tick_size);
}

void OrderBook::reset_book() {
    ask_order_book.fill(0.0);
    bid_order_book.fill(0.0);
    ask_bit_map.fill(0);
    bid_bit_map.fill(0);
    top_n_asks.fill(BookLevel{});
    top_n_bids.fill(BookLevel{});
    top_ask = BookLevel{};
    top_bid = BookLevel{};
    telemetry_data.book_telemetry = BookTelemetry{};
}

void OrderBook::set_top_n_prices() {
    top_n_bids = {};
    top_n_asks = {};

    if (top_bid.price > 0.0) {
        std::size_t count = 0;

        const std::size_t start_idx = price_to_index(top_bid.price);
        const std::size_t start_word = start_idx / 64;
        const std::size_t start_bit = start_idx % 64;

        for (std::size_t word = start_word; count < N; --word) {
            uint64_t bits = bid_bit_map[word];

            if (word == start_word) {
                // Keep only bits at or below start_bit. Higher bits are prices above top_bid.
                bits &= ((1ULL << (start_bit + 1)) - 1ULL);
            }

            while (bits != 0 && count < N) {
                // Find highest active bit in this word. For bids, higher bit = higher price.
                const std::size_t bit = 63 - std::countl_zero(bits);
                const std::size_t idx = word * 64 + bit;

                top_n_bids[count++] = BookLevel{
                    index_to_price(idx),
                    bid_order_book[idx]
                };

                // Clear the bit we just consumed so the next loop finds the next active level.
                bits &= ~(1ULL << bit);
            }

            if (word == 0) {
                break;
            }
        }
    }

    if (top_ask.price > 0.0) {
        std::size_t count = 0;

        const std::size_t start_idx = price_to_index(top_ask.price);
        const std::size_t start_word = start_idx / 64;
        const std::size_t start_bit = start_idx % 64;

        for (std::size_t word = start_word; word < ask_bit_map.size() && count < N; ++word) {
            uint64_t bits = ask_bit_map[word];

            if (word == start_word) {
                // Keep only bits at or above start_bit. Lower bits are prices below top_ask.
                bits &= (~0ULL << start_bit);
            }

            while (bits != 0 && count < N) {
                // Find lowest active bit in this word. For asks, lower bit = lower price.
                const std::size_t bit = std::countr_zero(bits);
                const std::size_t idx = word * 64 + bit;

                top_n_asks[count++] = BookLevel{
                    index_to_price(idx),
                    ask_order_book[idx]
                };

                // Clear the bit we just consumed so the next loop finds the next active level.
                bits &= ~(1ULL << bit);
            }
        }
    }

    top_bid = top_n_bids[0];
    top_ask = top_n_asks[0];
}

void OrderBook::set_book_telemetry() {
    for (std::size_t i = 0; i < N; ++i) {
        telemetry_data.book_telemetry.bids[i] = BookLevelTelemetry{
            .price = top_n_bids[i].price,
            .quantity = top_n_bids[i].quantity
        };
        telemetry_data.book_telemetry.asks[i] = BookLevelTelemetry{
            .price = top_n_asks[i].price,
            .quantity = top_n_asks[i].quantity
        };
    }
}

void OrderBook::set_price_level_active(const size_t& idx, const bool& isBid) {
    const std::size_t word = idx / 64;
    const std::size_t bit = idx % 64;
    const uint64_t mask = 1ULL << bit;

    if (isBid) {
        const uint64_t current_value = bid_bit_map[word];
        const uint64_t newValue = current_value | mask;
        bid_bit_map[word] = newValue;
    } else {
        const uint64_t current_value = ask_bit_map[word];
        const uint64_t newValue = current_value | mask;
        ask_bit_map[word] = newValue;
    }
}

void OrderBook::set_price_level_inactive(const size_t& idx, const bool& isBid) {
    const std::size_t word = idx / 64;
    const std::size_t bit = idx % 64;
    const uint64_t mask = 1ULL << bit;

    if (isBid) {
        const uint64_t current_value = bid_bit_map[word];
        const uint64_t newValue = current_value & ~mask;
        bid_bit_map[word] = newValue;
    } else {
        const uint64_t current_value = ask_bit_map[word];
        const uint64_t newValue = current_value & ~mask;
        ask_bit_map[word] = newValue;
    }
}
