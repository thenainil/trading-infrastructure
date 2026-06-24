//
// Created by Nainil Patel on 6/12/26.
//

#include "order-book/order_book.h"
#include <iostream>

BookEvent OrderBook::update_book(const MarketEvent& market_event) {
    BookEvent book_event{};

    // book_event.sequence = market_event.sequence;
    // book_event.exchange_ts = market_event.timestamp;
    // book_event.local_ts = std::chrono::system_clock::now();
    // book_event.symbol = market_event.symbol;
    // book_event.event_type = market_event.type;
    // book_event.top_bids = top_bids;
    // book_event.top_asks = top_asks;


    const std::vector<BookLevel>& asks = market_event.asks;
    const std::vector<BookLevel>& bids = market_event.bids;
    auto timestamp = market_event.timestamp;
    std::string type = market_event.type;
    std::string symbol = market_event.symbol;

    for (const BookLevel& ask : asks) {
        const size_t idx = price_to_index(ask.price);
        ask_order_book[idx] = ask.quantity;
        ask_bit_map[idx] = ask.quantity > 0 ? 1 : 0;
    }

    for (const BookLevel& bid : bids) {
        const size_t idx = price_to_index(bid.price);
        bid_order_book[idx] = bid.quantity;
        bid_bit_map[idx] = bid.quantity > 0 ? 1 : 0;
    }
}

double OrderBook::index_to_price(const std::size_t index) {
    return static_cast<double>(index) * tick_size;
}

std::size_t OrderBook::price_to_index(const double price) {
    return std::llround(price / tick_size);
}