//
// Created by Nainil Patel on 6/18/26.
//

#include "feed/feed.h"
#include <iostream>
#include <boost/asio/awaitable.hpp>
#include "common/spsc_ring.h"


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
        ring.produce(beast::buffers_to_string(buffer.data()));
        buffer.clear();
    }
}

void consume_kraken_websocket(net::io_context& ioc, KrakenSpscRing& ring) {
    const std::string host = "ws.kraken.com";
    const std::string port = "443";
    const std::string path = "/v2";

    const std::string subscribe =
        R"({"method":"subscribe","params":{"channel":"book","symbol":["BTC/USD"],"depth":10}})";
    
    const WebSocketConfig ws_config{host, port, path, subscribe, WsMessageMode::Text};

    net::co_spawn(ioc, open_websocket(ws_config, ring),
        [](std::exception_ptr e) {
            if (e) {
                try {
                    std::rethrow_exception(e);
                } catch (const std::exception& ex) {
                    std::cerr << "Kraken Session Error: " << ex.what() << '\n';
                }
            }
        });
}