#include <iostream>
#include <memory>
#include <thread>
#include <boost/asio/io_context.hpp>
#include "common/amqp_publisher.h"
#include "feed/feed.h"
#include "feed/parser.h"
#include "common/metadata.h"
#include "order-book/features.h"
#include "order-book/order_book.h"
#include "strategy/strategy.h"

constexpr uint64_t sequence_number {0};

int main() {
    //Metadata Telemetry
    MetadataSpscRing metadata_ring;
    boost::asio::io_context ioc_amqp;
    AmqpPublisher amqp_publisher(ioc_amqp, "amqp://guest:guest@localhost:5672/", "trade_metrics");

    std::jthread amqp_io_thread([&ioc_amqp] {
        ioc_amqp.run();
    });

    std::jthread telemetry_thread([&metadata_ring, &amqp_publisher] {
        Metadata out;
        while (true) {
            if (metadata_ring.consume(out)) {
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
                auto event = parse_kraken_book_event(out);

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
    std::unique_ptr<Features> features = std::make_unique<Features>();
    FeatureBookRing feature_book_ring;

    std::jthread order_book_thread([&order_book_ring, &feature_book_ring, &order_book, &features] {
        while (true) {
            MarketEvent out;
            if (order_book_ring.consume(out)) {
                auto success = order_book -> update_book(out);

                if (success) {
                    FeatureBook feature_book = *features -> calculate_features(*order_book);
                    feature_book_ring.produce(feature_book);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Strategy
    StrategyRing strategy_ring;
    std::jthread strategy_thread([&feature_book_ring, &strategy_ring] {
        while (true) {
            FeatureBook out;

            if (feature_book_ring.consume(out)) {
                auto event = determine_order_from_features(out);

                if (event) {
                    const StrategyEvent& strategy_event = *event;
                    strategy_ring.produce(strategy_event);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Risk


    return 0;
}