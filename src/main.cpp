#include <memory>
#include <thread>
#include <boost/asio/io_context.hpp>
#include "common/websocket_publisher.h"
#include "feed/feed.h"
#include "feed/parser.h"
#include "common/telemetry.h"
#include "order-book/features.h"
#include "order-book/order_book.h"
#include "strategy/strategy.h"

constexpr uint64_t sequence_number {0};

int main() {
    //Telemetry
    TelemetrySpscRing telemetry_ring{};
    boost::asio::io_context ioc_telemetry{};
    WebSocketPublisher telemetry_publisher(
        ioc_telemetry,
        telemetry_ws::parse_endpoint(std::getenv("METRICS_WS_URL")));

    std::jthread telemetry_io_thread([&ioc_telemetry] {
        ioc_telemetry.run();
    });

    std::jthread telemetry_submit_thread([&telemetry_ring, &telemetry_publisher] {
        TelemetryData telemetry_data;
        while (true) {
            if (telemetry_ring.consume(telemetry_data)) {
                telemetry_publisher.publishMessage(serialize_metadata_to_json(telemetry_data));
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Feed
    boost::asio::io_context ioc_ws;
    KrakenSpscRing kraken_ring;

    std::jthread kraken_consumer_thread([&ioc_ws, &kraken_ring]() {
        consume_kraken_websocket(ioc_ws, kraken_ring);
        ioc_ws.run();
    });

    //Parse
    OrderBookRing order_book_ring;
    std::jthread feed_producer_thread([&kraken_ring, &order_book_ring] {
        while (true) {
            ExchangeMessage exchange_message;
            if (kraken_ring.consume(exchange_message)) {
                auto event = parse_kraken_book_event(exchange_message);

                if (event) {
                    const MarketEvent& market_event = *event;
                    order_book_ring.produce(market_event);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Order Book + Features
    std::unique_ptr<OrderBook> order_book = std::make_unique<OrderBook>();
    std::unique_ptr<Features> features = std::make_unique<Features>();
    FeatureBookRing feature_book_ring;

    std::jthread order_book_thread([&order_book_ring, &feature_book_ring, &order_book, &features] {
        while (true) {
            MarketEvent market_event;
            if (order_book_ring.consume(market_event)) {
                auto update_success = order_book -> update_book(market_event);

                if (update_success) {
                    const FeatureBook feature_book = *features -> calculate_features(*order_book);
                    feature_book_ring.produce(feature_book);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Strategy
    std::jthread strategy_thread([&feature_book_ring, &telemetry_ring] {
        while (true) {
            FeatureBook feature_book;

            if (feature_book_ring.consume(feature_book)) {
                auto event = determine_order_from_features(feature_book);

                if (event) {
                    const StrategyEvent& strategy_event = *event;
                    telemetry_ring.produce(strategy_event.telemetry_data);
                }
            } else {
                std::this_thread::yield();
            }
        }
    });

    return 0;
}
