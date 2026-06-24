//
// Created by Nainil Patel on 6/21/26.
//

#include "common/metrics.h"
#include <iostream>
#include <ostream>
#include "feed/parser.h"

void log_message(const MarketEvent& message) {
    auto now = std::chrono::system_clock::now();
    auto diff = now - message.timestamp;
    auto micros = std::chrono::duration_cast<std::chrono::microseconds>(diff).count();
    std::cout << std::to_string(micros) << std::endl;
}
