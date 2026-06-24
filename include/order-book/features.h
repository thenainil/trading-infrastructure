//
// Created by Nainil Patel on 6/23/26.
//

#ifndef TRADING_INFRASTRUCTURE_FEATURES_H
#define TRADING_INFRASTRUCTURE_FEATURES_H
#include "order_book.h"

using FeatureRing = spsc_ring<BookEvent, 1024>;

#endif //TRADING_INFRASTRUCTURE_FEATURES_H
