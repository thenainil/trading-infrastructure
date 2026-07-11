//
// Created by Nainil Patel on 6/29/26.
//

#ifndef TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
#define TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H

#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>
#include <boost/asio/io_context.hpp>
#include <iostream>
#include <string>
#include <utility>

class LoggingAmqpHandler final : public AMQP::LibBoostAsioHandler {
public:
    explicit LoggingAmqpHandler(boost::asio::io_context& io_context) :
        AMQP::LibBoostAsioHandler(io_context) {}

    void onReady(AMQP::TcpConnection* connection) override {
        (void) connection;
        std::cerr << "RabbitMQ connection ready\n";
    }

    void onError(AMQP::TcpConnection* connection, const char* message) override {
        (void) connection;
        std::cerr << "RabbitMQ connection error: " << message << '\n';
    }

    void onClosed(AMQP::TcpConnection* connection) override {
        (void) connection;
        std::cerr << "RabbitMQ connection closed\n";
    }
};

class AmqpPublisher {
    LoggingAmqpHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    std::string routing_key;

public:
    AmqpPublisher(boost::asio::io_context& io_context, const std::string& amqpUrl, std::string queueName) :
        handler(io_context),
        connection(&handler, AMQP::Address(amqpUrl)),
        channel(&connection),
        routing_key(std::move(queueName)) {
        channel.onReady([this] {
            std::cerr << "RabbitMQ channel ready for queue/routing key: " << routing_key << '\n';
        });
        channel.onError([](const char* message) {
            std::cerr << "RabbitMQ channel error: " << message << '\n';
        });
    }

    void publishMessage(const std::string& message) {
        if (!channel.publish("", routing_key, message)) {
            std::cerr << "RabbitMQ publish was not accepted by the client channel\n";
        }
    }
};

#endif //TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
