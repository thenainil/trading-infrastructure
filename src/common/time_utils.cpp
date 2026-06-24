//
// Created by Nainil Patel on 6/19/26.
//

#include "common/time_utils.h"
#include <ctime>

namespace {
    int two_digits(const char* p) {
        return (p[0] - '0') * 10 + (p[1] - '0');
    }

    int four_digits(const char* p) {
        return (p[0] - '0') * 1000 +
               (p[1] - '0') * 100 +
               (p[2] - '0') * 10 +
               (p[3] - '0');
    }
}

std::chrono::system_clock::time_point parse_iso_utc_timestamp(std::string_view timestamp) {
    if (timestamp.size() != 27) {
        return {};
    }

    const char* ts = timestamp.data();

    std::tm tm{};
    tm.tm_year = four_digits(ts) - 1900;
    tm.tm_mon  = two_digits(ts + 5) - 1;
    tm.tm_mday = two_digits(ts + 8);
    tm.tm_hour = two_digits(ts + 11);
    tm.tm_min  = two_digits(ts + 14);
    tm.tm_sec  = two_digits(ts + 17);

    const int micros =
        (ts[20] - '0') * 100000 +
        (ts[21] - '0') * 10000 +
        (ts[22] - '0') * 1000 +
        (ts[23] - '0') * 100 +
        (ts[24] - '0') * 10 +
        (ts[25] - '0');

    std::time_t seconds = timegm(&tm);

    return std::chrono::system_clock::from_time_t(seconds) +
           std::chrono::microseconds{micros};
}