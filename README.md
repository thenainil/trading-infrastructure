# Trading Infrastructure

A C++23 trading infrastructure project focused on market data ingestion, parsing, and order book construction.

This is my flagship C++ project as I transition from Java into lower-level systems programming. The current codebase is intentionally small and still early-stage, but it already contains the core shape of a real-time market data pipeline: a Kraken WebSocket consumer, JSON parsing, single-producer/single-consumer queues, timestamp handling, and an in-memory order book update path.

## Current Status

This project is a work in progress. It is not production trading software and should not be used to place orders or make financial decisions.

Implemented so far:

- Connects to Kraken's public WebSocket API
- Subscribes to BTC/USD order book updates
- Parses Kraken book messages with `simdjson`
- Moves data between pipeline stages with a lock-free SPSC ring buffer
- Converts exchange timestamps into C++ chrono time points
- Applies price level updates into an in-memory order book structure
- Builds with CMake, Conan, Boost, OpenSSL, and simdjson

Still in progress:

- Emitting complete `BookEvent` snapshots from the order book
- Best bid/ask extraction from the bitmap-backed book
- Sequence validation and gap handling
- Reconnect and resubscription behavior
- Tests and benchmarks
- Cleaner shutdown behavior for worker threads
- More complete metrics and observability

## Architecture

The current data flow is:

```text
Kraken WebSocket
    -> raw JSON messages
    -> SPSC ring buffer
    -> simdjson parser
    -> normalized MarketEvent
    -> SPSC ring buffer
    -> OrderBook update path
```

Main components:

- `src/feed/` - WebSocket consumption and Kraken message parsing
- `src/order_book/` - order book update logic
- `src/common/` - shared utilities such as SPSC queues, timestamp parsing, and metrics
- `include/` - public headers for the project modules

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio / Boost.Beast
- OpenSSL
- simdjson
- `std::jthread`
- Custom SPSC ring buffer

## Build

Prerequisites:

- A C++23-capable compiler
- CMake
- Conan

Configure with the provided CMake preset:

```bash
cmake --preset conan
```

Build:

```bash
cmake --build "$HOME/.cmake-build/trading-infrastructure/conan"
```

Run:

```bash
"$HOME/.cmake-build/trading-infrastructure/conan/trading_infrastructure"
```

There is also a release preset:

```bash
cmake --preset release
cmake --build "$HOME/.cmake-build/trading-infrastructure/release"
```

## Project Goals

The long-term goal is to build a small but serious trading infrastructure system that demonstrates:

- Low-latency C++ design
- Market data feed handling
- Order book construction
- Event-driven architecture
- Concurrency and memory-layout awareness
- Testing, profiling, and benchmarking discipline

This repository is meant to show the learning path as well as the final system. Some code will be naive at first, then hardened as the project grows.

## Notes

The project currently disables TLS peer verification in the WebSocket client while the infrastructure is being built out. That should be changed before treating the feed client as production-ready.

Generated build directories, IDE files, and compiled binaries should not be committed.
