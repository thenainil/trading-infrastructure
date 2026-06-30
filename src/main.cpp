#include <iostream>
#include <memory>
#include <thread>
#include <boost/asio/io_context.hpp>
#include "common/amqp_publisher.h"
#include "feed/feed.h"
#include "feed/parser.h"
#include "common/telemetry.h"
#include "order-book/order_book.h"

constexpr uint64_t sequence_number {0};

int main() {
    //Telemetry
    TelemetrySpscRing telemetry_ring;
    boost::asio::io_context ioc_amqp;
    AmqpPublisher amqp_publisher(ioc_amqp, "amqp://guest:guest@localhost:5672/", "trade_metrics");

    std::jthread amqp_io_thread([&ioc_amqp] {
        ioc_amqp.run();
    });

    std::jthread telemetry_thread([&telemetry_ring, &amqp_publisher] {
        Telemetry out;
        while (true) {
            if (telemetry_ring.consume(out)) {
                amqp_publisher.publishMessage("Hello World!");
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Feed Consumption + Parsing
    boost::asio::io_context ioc_ws;
    KrakenSpscRing kraken_ring;
    OrderBookRing order_book_ring;

    std::jthread feed_consumer_thread([&ioc_ws, &kraken_ring]() {
        consume_kraken_websocket(ioc_ws, kraken_ring);
        ioc_ws.run();
    });

    std::jthread feed_producer_thread([&kraken_ring, &order_book_ring] {
        while (true) {
            ExchangeMessage out;
            if (kraken_ring.consume(out)) {
                auto event = parse_kraken_book_event(out.data);

                if (event) {
                    const MarketEvent& market_event = *event;
                    order_book_ring.produce(market_event);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Order Book
    std::unique_ptr<OrderBook> order_book = std::make_unique<OrderBook>();
    std::jthread order_book_thread([&order_book_ring, &order_book] {
        while (true) {
            MarketEvent out;
            if (order_book_ring.consume(out)) {
                auto event = order_book -> update_book(out);

                if (event) {
                    const BookEvent& book_event = *event;
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    return 0;
}