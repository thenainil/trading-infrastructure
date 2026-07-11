//
// Created by Nainil Patel on 6/29/26.
//

#ifndef TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
#define TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H

#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>
#include <amqpcpp/openssl.h>
#include <boost/asio/io_context.hpp>
#include <dlfcn.h>
#include <iostream>
#include <openssl/ssl.h>
#include <string>
#include <utility>

struct OpenSslInit {
    OpenSslInit() {
        AMQP::openssl(dlopen("libssl.so", RTLD_LAZY));
        OPENSSL_init_ssl(0, nullptr);
    }
};

class AmqpHandler final : public AMQP::LibBoostAsioHandler {
public:
    explicit AmqpHandler(boost::asio::io_context& io_context) :
        AMQP::LibBoostAsioHandler(io_context) {}

    void onError(AMQP::TcpConnection* connection, const char* message) override {
        (void) connection;
        std::cerr << "RabbitMQ connection error: " << message << '\n';
    }
};

class AmqpPublisher {
    OpenSslInit openssl_init;
    AmqpHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    std::string routing_key;

public:
    AmqpPublisher(boost::asio::io_context& io_context, const std::string& amqpUrl, std::string queueName) :
        openssl_init(),
        handler(io_context),
        connection(&handler, AMQP::Address(amqpUrl)),
        channel(&connection),
        routing_key(std::move(queueName)) {
        channel.onError([](const char* message) {
            std::cerr << "RabbitMQ channel error: " << message << '\n';
        });
    }

    void publishMessage(const std::string& message) {
        channel.publish("", routing_key, message);
    }
};

#endif //TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
