//
// Created by Nainil Patel on 6/21/26.
//

#ifndef TRADING_INFRASTRUCTURE_METRICS_H
#define TRADING_INFRASTRUCTURE_METRICS_H
#include "spsc_ring.h"
#include "feed/parser.h"

using LogSpscRing = spsc_ring<MarketEvent, 1024>;

void log_message(const MarketEvent& message);

#endif //TRADING_INFRASTRUCTURE_METRICS_H
