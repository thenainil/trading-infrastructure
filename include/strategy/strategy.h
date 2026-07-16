//
// Created by Nainil Patel on 7/2/26.
//

#ifndef TRADING_INFRASTRUCTURE_STRATEGY_H
#define TRADING_INFRASTRUCTURE_STRATEGY_H

#include "order-book/features.h"

enum class OrderDecision {
    STRONG_BUY,
    STRONG_SELL,
    BUY,
    SELL,
    WAIT
};

struct StrategyEvent {
    TelemetryData telemetry_data;
    OrderDecision order_decision;
};

std::optional<StrategyEvent> determine_order_from_features(const FeatureBook& features);

#endif //TRADING_INFRASTRUCTURE_STRATEGY_H

