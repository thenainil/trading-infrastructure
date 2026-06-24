//
// Created by Nainil Patel on 6/19/26.
//

#ifndef TRADING_INFRASTRUCTURE_PARSER_H
#define TRADING_INFRASTRUCTURE_PARSER_H

#include <chrono>
#include <vector>
#include <string>
#include <string_view>
#include <optional>

struct BookLevel {
    double price{};
    double quantity{};
};

struct MarketEvent {
    std::string type;
    std::string symbol;
    std::vector<BookLevel> bids;
    std::vector<BookLevel> asks;
    std::chrono::system_clock::time_point timestamp;
};

std::optional<MarketEvent> parse_kraken_book_event(std::string_view event);

#endif //TRADING_INFRASTRUCTURE_PARSER_H
