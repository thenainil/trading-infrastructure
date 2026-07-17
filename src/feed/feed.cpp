//
// Created by Nainil Patel on 6/18/26.
//

#include "feed/feed.h"
#include <iostream>
#include <boost/asio/awaitable.hpp>
#include "templates/spsc_ring.h"

namespace {
    uint64_t next_monotonic_id() {
        static uint64_t last_us = 0;
        static uint64_t same_us_counter = 0;

        const auto now = std::chrono::system_clock::now().time_since_epoch();
        uint64_t now_us = static_cast<uint64_t>(
            std::chrono::duration_cast<std::chrono::microseconds>(now).count()
        );

        if (now_us < last_us) {
            now_us = last_us;
        }

        if (now_us == last_us) {
            ++same_us_counter;
        } else {
            last_us = now_us;
            same_us_counter = 0;
        }

        constexpr uint64_t max_events_per_us = 1'000;

        return now_us * max_events_per_us + same_us_counter;
    }
}

boost::asio::awaitable<void> open_websocket(WebSocketConfig web_socket_config, KrakenSpscRing& ring) {

    std::string host = web_socket_config.host;
    std::string port = web_socket_config.port;
    std::string path = web_socket_config.path;
    std::optional<std::string> subscribe = web_socket_config.subscribe;
    WsMessageMode ws_message_mode = web_socket_config.ws_message_mode;

    ssl::context ctx{ssl::context::tlsv12_client};
    ctx.set_verify_mode(ssl::verify_none); //TODO: Update to verify_peer before PRODUCTION

    auto executor = co_await net::this_coro::executor;
    tcp::resolver resolve{executor};
    websocket::stream<ssl::stream<tcp::socket>> ws{executor, ctx};
    ws.set_option(websocket::stream_base::timeout::suggested(beast::role_type::client));

    const auto results = co_await resolve.async_resolve(host, port, net::use_awaitable);
    const auto endpoint = co_await net::async_connect(beast::get_lowest_layer(ws), results, net::use_awaitable);

    if (!SSL_set_tlsext_host_name(ws.next_layer().native_handle(), host.c_str())) {
        throw beast::system_error{
            beast::error_code{static_cast<int>(::ERR_get_error()),
                              net::error::get_ssl_category()}};
    }

    const std::string host_header = host + ':' + std::to_string(endpoint.port());
    co_await ws.next_layer().async_handshake(ssl::stream_base::client, net::use_awaitable);
    co_await ws.async_handshake(host_header, path, net::use_awaitable);

    if (subscribe) {
        if (ws_message_mode == WsMessageMode::Text) ws.text(true);
        co_await ws.async_write(net::buffer(*subscribe), net::use_awaitable);
    }

    beast::flat_buffer buffer;
    while (true) {
        co_await ws.async_read(buffer, net::use_awaitable);

        ring.produce(ExchangeMessage{
            TelemetryData{
                LatencyMetrics{
                    .message_received_wall_ts = std::chrono::system_clock::now(),
                    .message_received_ts = std::chrono::steady_clock::now()
                },
                Identifier{
                    .monotonic_id = next_monotonic_id()
                }
            },
            beast::buffers_to_string(buffer.data())
        });

        buffer.clear();
    }
}

namespace {
    boost::asio::awaitable<void> consume_with_reconnect(WebSocketConfig ws_config, KrakenSpscRing& ring) {
        auto executor = co_await net::this_coro::executor;
        net::steady_timer reconnect_timer{executor};

        while (true) {
            try {
                co_await open_websocket(ws_config, ring);
            } catch (const std::exception& ex) {
                std::cerr << "Kraken session disconnected: " << ex.what() << '\n';
            }

            std::cerr << "Reconnecting to Kraken WebSocket in 1s\n";
            reconnect_timer.expires_after(std::chrono::seconds(1));
            co_await reconnect_timer.async_wait(net::use_awaitable);
        }
    }
}

void consume_kraken_websocket(net::io_context& ioc, KrakenSpscRing& ring) {
    const std::string host = "ws.kraken.com";
    const std::string port = "443";
    const std::string path = "/v2";

    const std::string subscribe =
        R"({"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":1000}})";
    
    const WebSocketConfig ws_config{host, port, path, subscribe, WsMessageMode::Text};

    net::co_spawn(ioc, consume_with_reconnect(ws_config, ring),
        [](std::exception_ptr e) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception& ex) {
                    std::cerr << "Kraken reconnect loop error: " << ex.what() << '\n';
                }
            }
        });
}
