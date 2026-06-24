cmake_minimum_required(VERSION 4.2)

set(CMAKE_PROJECT_TOP_LEVEL_INCLUDES ${CMAKE_SOURCE_DIR}/conan_provider.cmake)

project(trading_infrastructure)

set(CMAKE_CXX_STANDARD 23)

add_library(trading_infrastructure_lib
        src/order_book/order_book.cpp
        src/common/time_utils.cpp
        src/common/metrics.cpp
        src/feed/feed.cpp
        src/feed/parser.cpp
        src/common/metrics.cpp)

target_include_directories(trading_infrastructure_lib
        PUBLIC
        include)

add_executable(trading_infrastructure
        src/main.cpp
        include/order-book/features.h)

target_link_libraries(trading_infrastructure PRIVATE trading_infrastructure_lib)

find_package(Boost REQUIRED CONFIG)
find_package(OpenSSL REQUIRED)
find_package(simdjson REQUIRED CONFIG)
find_package(Threads REQUIRED)

target_link_libraries(trading_infrastructure_lib
        PUBLIC
        Boost::headers
        OpenSSL::SSL
        OpenSSL::Crypto
        simdjson::simdjson
        Threads::Threads)
