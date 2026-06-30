//
// Created by Nainil Patel on 6/18/26.
//
#ifndef TRADING_INFRASTRUCTURE_FEED_H
#define TRADING_INFRASTRUCTURE_FEED_H

#include <optional>
#include <boost/beast/core.hpp>
#include <boost/beast/core/buffers_to_string.hpp>
#include <boost/beast/http.hpp>
#include <boost/beast/websocket.hpp>
#include <boost/beast/websocket/ssl.hpp>
#include <boost/asio/co_spawn.hpp>
#include <boost/asio/awaitable.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/ssl.hpp>
#include "templates/spsc_ring.h"

namespace beast     = boost::beast;
namespace http      = beast::http;
namespace websocket = beast::websocket;
namespace net       = boost::asio;
namespace ssl       = boost::asio::ssl;

using tcp       = boost::asio::ip::tcp;

struct ExchangeMessage {
    std::chrono::high_resolution_clock::time_point received_ts;
    std::string data;
};

using KrakenSpscRing = spsc_ring<ExchangeMessage, 1024>;

enum class WsMessageMode {
    Text,
    Binary
};

struct WebSocketConfig {
    std::string host;
    std::string port;
    std::string path;
    std::optional<std::string> subscribe;
    WsMessageMode ws_message_mode;
};

net::awaitable<void> open_websocket(WebSocketConfig web_socket_config, KrakenSpscRing& ring);
void consume_kraken_websocket(net::io_context& ioc, KrakenSpscRing& ring);

#endif //TRADING_INFRASTRUCTURE_FEED_H
