//
// Created by Nainil Patel on 6/21/26.
//

#ifndef TRADING_INFRASTRUCTURE_METRICS_H
#define TRADING_INFRASTRUCTURE_METRICS_H

#include <boost/asio/io_context.hpp>
#include "templates/spsc_ring.h"
#include "feed/parser.h"
#include "order-book/order_book.h"

struct Telemetry {

    MarketEvent market_event;
    BookEvent book_event;

};

using TelemetrySpscRing = spsc_ring<Telemetry, 1024>;

void publish_telemetry_message(const Telemetry& telemetry, boost::asio::io_context& ioc);

#endif //TRADING_INFRASTRUCTURE_METRICS_H
