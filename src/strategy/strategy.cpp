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
    return strategy_event;
}
