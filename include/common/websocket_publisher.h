#ifndef TRADING_INFRASTRUCTURE_WEBSOCKET_PUBLISHER_H
#define TRADING_INFRASTRUCTURE_WEBSOCKET_PUBLISHER_H

#include <boost/asio/io_context.hpp>
#include <boost/asio/executor_work_guard.hpp>
#include <boost/asio/ip/tcp.hpp>
#include <boost/asio/post.hpp>
#include <boost/asio/steady_timer.hpp>
#include <boost/beast/core.hpp>
#include <boost/beast/websocket.hpp>
#include <chrono>
#include <deque>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <utility>

namespace telemetry_ws {
    namespace net = boost::asio;
    namespace beast = boost::beast;
    namespace websocket = beast::websocket;
    using tcp = net::ip::tcp;

    struct Endpoint {
        std::string host{"127.0.0.1"};
        std::string port{"3000"};
        std::string path{"/ingest"};
    };

    inline Endpoint parse_endpoint(const char* raw_url) {
        if (raw_url == nullptr || std::string_view{raw_url}.empty()) {
            return {};
        }

        std::string url{raw_url};
        constexpr std::string_view scheme = "ws://";
        if (url.rfind(scheme, 0) == 0) {
            url.erase(0, scheme.size());
        }

        Endpoint endpoint;
        const auto path_pos = url.find('/');
        const std::string authority = path_pos == std::string::npos ? url : url.substr(0, path_pos);
        endpoint.path = path_pos == std::string::npos ? "/" : url.substr(path_pos);

        const auto port_pos = authority.rfind(':');
        if (port_pos == std::string::npos) {
            endpoint.host = authority;
        } else {
            endpoint.host = authority.substr(0, port_pos);
            endpoint.port = authority.substr(port_pos + 1);
        }

        if (endpoint.host.empty()) endpoint.host = "127.0.0.1";
        if (endpoint.port.empty()) endpoint.port = "3000";
        if (endpoint.path.empty()) endpoint.path = "/ingest";

        return endpoint;
    }
}

class WebSocketPublisher {
    boost::asio::io_context& io_context;
    boost::asio::executor_work_guard<boost::asio::io_context::executor_type> work_guard;
    telemetry_ws::Endpoint endpoint;
    telemetry_ws::tcp::resolver resolver;
    boost::asio::steady_timer reconnect_timer;
    std::unique_ptr<telemetry_ws::websocket::stream<telemetry_ws::tcp::socket>> ws;
    std::deque<std::string> pending;
    bool connected{false};
    bool connecting{false};
    bool writing{false};

public:
    explicit WebSocketPublisher(boost::asio::io_context& io_context, telemetry_ws::Endpoint endpoint) :
        io_context(io_context),
        work_guard(boost::asio::make_work_guard(io_context)),
        endpoint(std::move(endpoint)),
        resolver(io_context),
        reconnect_timer(io_context) {
        boost::asio::post(io_context, [this] {
            connect();
        });
    }

    void publishMessage(std::string message) {
        boost::asio::post(io_context, [this, message = std::move(message)]() mutable {
            pending.emplace_back(std::move(message));
            if (pending.size() > 10'000) {
                pending.pop_front();
            }

            if (!connected && !connecting) {
                connect();
                return;
            }

            write_next();
        });
    }

private:
    void connect() {
        if (connected || connecting) return;

        connecting = true;
        ws = std::make_unique<telemetry_ws::websocket::stream<telemetry_ws::tcp::socket>>(io_context);

        resolver.async_resolve(endpoint.host, endpoint.port,
            [this](const telemetry_ws::beast::error_code& ec, telemetry_ws::tcp::resolver::results_type results) {
                if (ec) {
                    fail("resolve", ec);
                    return;
                }

                boost::asio::async_connect(ws->next_layer(), results,
                    [this](const telemetry_ws::beast::error_code& ec, const telemetry_ws::tcp::endpoint&) {
                        if (ec) {
                            fail("connect", ec);
                            return;
                        }

                        ws->text(true);
                        const std::string host_header = endpoint.host + ':' + endpoint.port;
                        ws->async_handshake(host_header, endpoint.path,
                            [this](const telemetry_ws::beast::error_code& ec) {
                                if (ec) {
                                    fail("handshake", ec);
                                    return;
                                }

                                connecting = false;
                                connected = true;
                                std::cerr << "Telemetry WebSocket connected to ws://"
                                          << endpoint.host << ':' << endpoint.port
                                          << endpoint.path << '\n';
                                write_next();
                            });
                    });
            });
    }

    void write_next() {
        if (!connected || writing || pending.empty()) return;

        writing = true;
        ws->async_write(boost::asio::buffer(pending.front()),
            [this](const telemetry_ws::beast::error_code& ec, std::size_t) {
                writing = false;
                if (ec) {
                    fail("write", ec);
                    return;
                }

                pending.pop_front();
                write_next();
            });
    }

    void fail(const char* operation, const telemetry_ws::beast::error_code& ec) {
        std::cerr << "Telemetry WebSocket " << operation << " error: " << ec.message() << '\n';
        connected = false;
        connecting = false;
        writing = false;
        ws.reset();

        reconnect_timer.expires_after(std::chrono::seconds(1));
        reconnect_timer.async_wait([this](const telemetry_ws::beast::error_code& ec) {
            if (!ec) connect();
        });
    }
};

#endif //TRADING_INFRASTRUCTURE_WEBSOCKET_PUBLISHER_H
