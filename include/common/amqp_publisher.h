//
// Created by Nainil Patel on 6/29/26.
//

#ifndef TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
#define TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H

#include <amqpcpp.h>
#include <amqpcpp/libboostasio.h>
#include <boost/asio/io_context.hpp>
#include <string>
#include <utility>

class AmqpPublisher {
    AMQP::LibBoostAsioHandler handler;
    AMQP::TcpConnection connection;
    AMQP::TcpChannel channel;
    std::string routing_key;

public:
    AmqpPublisher(boost::asio::io_context& io_context, const std::string& amqpUrl, std::string queueName) :
        handler(io_context),
        connection(&handler, AMQP::Address(amqpUrl)),
        channel(&connection),
        routing_key(std::move(queueName)) {}

    void publishMessage(const std::string& message) {
        channel.publish("", routing_key, message);
    }
};

#endif //TRADING_INFRASTRUCTURE_AMQP_PUBLISHER_H
