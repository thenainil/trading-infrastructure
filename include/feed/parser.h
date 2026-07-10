//
// Created by Nainil Patel on 6/19/26.
//

#ifndef TRADING_INFRASTRUCTURE_PARSER_H
#define TRADING_INFRASTRUCTURE_PARSER_H

#include <chrono>
#include <vector>
#include <string>
#include <optional>

#include "feed.h"
#include "common/metadata.h"

struct BookLevel {
    double price{};
    double quantity{};
};

struct MarketEvent {
    Metadata metadata;
    std::string type;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
};

std::optional<MarketEvent> parse_kraken_book_event(const ExchangeMessage& event);

#endif //TRADING_INFRASTRUCTURE_PARSER_H