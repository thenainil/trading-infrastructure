//
// Created by Nainil Patel on 7/2/26.
//

#include "strategy/strategy.h"

#include <iostream>

const char* to_string(const OrderDecision decision) {
    switch (decision) {
        case OrderDecision::STRONG_BUY: return "STRONG_BUY";
        case OrderDecision::STRONG_SELL: return "STRONG_SELL";
        case OrderDecision::BUY: return "BUY";
        case OrderDecision::SELL: return "SELL";
        case OrderDecision::WAIT: return "WAIT";
    }
    return "UNKNOWN";
}

std::optional<StrategyEvent> determine_order_from_features(const FeatureBook& features) {
    StrategyEvent strategy_event{};
    strategy_event.metadata = features.metadata;

    //default
    strategy_event.order_decision = OrderDecision::WAIT;

    if (features.tick_spread <= 5) {
        if (features.microprice_edge_tick > 0.3 &&
                features.top_level_imbalance > 0.6 &&
                features.top_n_imbalance > 0.5 &&
                features.ofi > 0 &&
                features.microprice > features.mid_price
                && features.top_bid_qty > features.top_ask_qty) {
            strategy_event.order_decision = OrderDecision::STRONG_BUY;
        } else if (features.microprice_edge_tick < -0.3 &&
                features.top_level_imbalance < -0.6 &&
                features.top_n_imbalance < -0.5 &&
                features.ofi < 0 &&
                features.microprice < features.mid_price
                && features.top_ask_qty > features.top_bid_qty) {
            strategy_event.order_decision = OrderDecision::STRONG_SELL;
        } else if (features.microprice_edge_tick > 0.1 &&
                features.top_level_imbalance > 0.3 &&
                features.top_n_imbalance > 0.2 &&
                features.ofi > 0 &&
                features.microprice > features.mid_price
                && features.top_bid_qty > features.top_ask_qty) {
            strategy_event.order_decision = OrderDecision::BUY;
        } else if (features.microprice_edge_tick < -0.1 &&
                features.top_level_imbalance < -0.3 &&
                features.top_n_imbalance < -0.2 &&
                features.ofi < 0 &&
                features.microprice < features.mid_price
                && features.top_ask_qty > features.top_bid_qty) {
            strategy_event.order_decision = OrderDecision::SELL;
        }
    }

    strategy_event.metadata.strategy_decision_complete_ts = std::chrono::steady_clock::now();

    //---------------------------------------------------------------------------------------------
        std::cout << "Network Latency: "
          << std::chrono::duration_cast<std::chrono::microseconds>(
                 features.metadata.message_received_wall_ts - features.metadata.exchange_ts
             ).count()
          << "us\n";

    std::cout << "Parse Latency: "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(
                     features.metadata.parse_complete_ts - features.metadata.message_received_ts
                 ).count()
              << "ns\n";

    std::cout << "Order Book Latency: "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(
                     features.metadata.order_book_complete_ts - features.metadata.parse_complete_ts
                 ).count()
              << "ns\n";

    std::cout << "Feature Latency: "
              << std::chrono::duration_cast<std::chrono::nanoseconds>(
                     features.metadata.feature_calculation_complete_ts - features.metadata.order_book_complete_ts
                 ).count()
              << "ns\n";
    std::cout << "Strategy Latency: "
          << std::chrono::duration_cast<std::chrono::nanoseconds>(
                 strategy_event.metadata.strategy_decision_complete_ts - features.metadata.feature_calculation_complete_ts
             ).count()
          << "ns\n";
    std::cout << "-------------------------\n";
    std::cout << "Top Bid: " << features.top_bid << '\n';
    std::cout << "Top Ask: " << features.top_ask << '\n';
    std::cout << "Top Bid Qty: " << features.top_bid_qty << '\n';
    std::cout << "Top Ask Qty: " << features.top_ask_qty << '\n';
    std::cout << "Spread: " << features.spread << '\n';
    std::cout << "Tick Spread: " << features.tick_spread << '\n';
    std::cout << "Mid Price: " << features.mid_price << '\n';
    std::cout << "Microprice: " << features.microprice << '\n';
    std::cout << "Microprice Edge Tick: " << features.microprice_edge_tick << '\n';
    std::cout << "Top Level Imbalance: " << features.top_level_imbalance << '\n';
    std::cout << "Top N Imbalance: " << features.top_n_imbalance << '\n';
    std::cout << "Top Level OFI: " << features.ofi << '\n';
    std::cout << "Top N OFI: " << features.top_n_ofi << '\n';
    std::cout << "Strategy Decision: " << to_string(strategy_event.order_decision) << '\n';
    std::cout << "--------------------------------------------------" << '\n';
    //---------------------------------------------------------------------------------------------

    return strategy_event;
}
