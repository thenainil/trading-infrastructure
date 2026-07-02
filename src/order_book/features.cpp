//
// Created by Nainil Patel on 6/30/26.
//

#include "order-book/features.h"

namespace {
    double calculate_n_depth_imbalance(const std::size_t& depth,
        const std::array<BookLevel, N>& top_n_bids,
        const std::array<BookLevel, N>& top_n_asks) {

        double cum_bid_size {};
        double cum_ask_size {};

        for (int i = 0; i < depth; i++) {

            const double bid_qty = top_n_bids[i].quantity;
            const double ask_qty = top_n_asks[i].quantity;

            if (bid_qty == 0.0 || ask_qty == 0.0) {
                break;
            }

            cum_bid_size += bid_qty;
            cum_ask_size += ask_qty;
        }

        return (cum_bid_size - cum_ask_size) / (cum_bid_size + cum_ask_size);
    }

    double quantity_at_price(const std::array<BookLevel, N>& levels, const double price) {
        for (const BookLevel& level : levels) {
            if (level.price == 0.0) {
                break;
            }

            if (level.price == price) {
                return level.quantity;
            }
        }
        return 0.0;
    }

    double calculate_n_depth_ofi(const std::size_t& depth,
        const std::array<BookLevel, N>& top_n_bids, const std::array<BookLevel, N>& top_n_asks,
        bool& previous_book_exists, std::array<BookLevel, N>& previous_top_bids, std::array<BookLevel, N>& previous_top_asks) {

        double bid_ofi{};
        double ask_ofi{};

        if (previous_book_exists) {
            for (std::size_t i = 0; i < depth; ++i) {
                const BookLevel& current_bid = top_n_bids[i];

                if (current_bid.price != 0.0) {
                    const double previous_qty = quantity_at_price(previous_top_bids, current_bid.price);
                    bid_ofi += current_bid.quantity - previous_qty;
                }

                const BookLevel& previous_bid = previous_top_bids[i];

                if (previous_bid.price != 0.0) {
                    const double current_qty = quantity_at_price(top_n_bids, previous_bid.price);
                    if (current_qty == 0.0) {
                        bid_ofi -= previous_bid.quantity;
                    }
                }

                const BookLevel& current_ask = top_n_asks[i];

                if (current_ask.price != 0.0) {
                    const double previous_qty = quantity_at_price(previous_top_asks, current_ask.price);
                    ask_ofi += previous_qty - current_ask.quantity;
                }

                const BookLevel& previous_ask = previous_top_asks[i];
                if (previous_ask.price != 0.0) {
                    const double current_qty = quantity_at_price(top_n_asks, previous_ask.price);
                    if (current_qty == 0.0) {
                        ask_ofi += previous_ask.quantity;
                    }
                }
            }
        }

        previous_top_bids = top_n_bids;
        previous_top_asks = top_n_asks;
        previous_book_exists = true;

        return bid_ofi + ask_ofi;;
    }

}

std::optional<FeatureBook> Features::calculate_features(const OrderBook& order_book) {
    FeatureBook feature_book{};
    feature_book.metadata = order_book.metadata;

    //Base
    const std::array<BookLevel, N> top_n_bids = order_book.get_top_n_levels(true);
    const std::array<BookLevel, N> top_n_asks = order_book.get_top_n_levels(false);
    feature_book.top_bid = top_n_bids[0].price;
    feature_book.top_ask = top_n_asks[0].price;
    feature_book.top_bid_qty = top_n_bids[0].quantity;
    feature_book.top_ask_qty = top_n_asks[0].quantity;

    //Derived -> Single Level
    feature_book.mid_price = (feature_book.top_ask + feature_book.top_bid) / 2;
    feature_book.microprice = ((feature_book.top_ask * feature_book.top_bid_qty) +
        (feature_book.top_bid * feature_book.top_ask_qty)) / (feature_book.top_bid_qty + feature_book.top_ask_qty);
    feature_book.microprice_edge_tick = (feature_book.microprice - feature_book.mid_price) / tick_size;
    feature_book.spread = feature_book.top_ask - feature_book.top_bid;
    feature_book.tick_spread = feature_book.spread / tick_size;
    feature_book.top_level_imbalance = (feature_book.top_bid_qty - feature_book.top_ask_qty) /
        (feature_book.top_bid_qty + feature_book.top_ask_qty);
    feature_book.ofi = calculate_n_depth_ofi(1, top_n_bids, top_n_asks,
        previous_book_exists, previous_top_bids, previous_top_asks);

    //Derived -> Multi-Level
    feature_book.top_n_imbalance = calculate_n_depth_imbalance(3, top_n_bids, top_n_asks);
    feature_book.top_n_ofi = calculate_n_depth_ofi(3, top_n_bids, top_n_asks,
        previous_book_exists, previous_top_bids, previous_top_asks);

    feature_book.metadata.feature_calculation_complete_ts = std::chrono::steady_clock::now();
    return feature_book;
}

/*
1. So many other features to write, also clean up the namespace, it's all getting too dirty and hard to read.
*/