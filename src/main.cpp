#include <iostream>
#include <memory>
#include <thread>
#include <boost/asio/io_context.hpp>
#include "feed/feed.h"
#include "feed/parser.h"
#include "common/metrics.h"
#include "order-book/order_book.h"

int main() {
    //Metrics
    LogSpscRing log_spsc_ring;

    std::jthread metrics_thread([&log_spsc_ring] {
        MarketEvent out;
        while (true) {
            if (log_spsc_ring.consume(out)) {
                log_message(out);
            } else {
                std::this_thread::yield();
            }
        }
    });

    //Feed Consumption + Parsing
    boost::asio::io_context ioc;
    KrakenSpscRing kraken_ring;
    OrderBookRing order_book_ring;

    std::jthread feed_consumer_thread([&ioc, &kraken_ring]() {
        consume_kraken_websocket(ioc, kraken_ring);
        ioc.run();
    });

    std::jthread feed_producer_thread([&kraken_ring, &order_book_ring] {
        while (true) {
            std::string out;
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
    std::jthread order_book_thread([&order_book_ring, &order_book] {
        while (true) {
            MarketEvent out;
            if (order_book_ring.consume(out)) {
                BookEvent book_event = order_book -> update_book(out);
            } else {
                std::this_thread::yield();
            }
        }
    });

    return 0;
}