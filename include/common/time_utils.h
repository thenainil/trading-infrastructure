//
// Created by Nainil Patel on 6/19/26.
//

#ifndef TRADING_INFRASTRUCTURE_TIME_UTILS_H
#define TRADING_INFRASTRUCTURE_TIME_UTILS_H

#include <chrono>
#include <string_view>

std::chrono::system_clock::time_point parse_iso_utc_timestamp(std::string_view timestamp);

#endif //TRADING_INFRASTRUCTURE_TIME_UTILS_H
