# Trading Infrastructure

A C++23 trading-infrastructure project focused on real-time market-data
ingestion, low-overhead inter-thread handoff, order book construction,
microstructure feature calculation, strategy signaling, and telemetry export.

This is not production trading software and should not be used to place orders
or make financial decisions. The goal is to build an inspectable systems project
that exercises the mechanics behind latency-sensitive trading infrastructure.

## Repository Layout

```text
include/  Public headers
src/      C++ source files
```

Build and dependency files live at the repository root:

```text
CMakeLists.txt
CMakePresets.json
conanfile.txt
conan_provider.cmake
```

## Current Status

Implemented:

- Connects to Kraken's public WebSocket v2 API over TLS
- Subscribes to live BTC/USD book updates
- Uses Boost.Asio and Boost.Beast for asynchronous WebSocket I/O
- Parses Kraken book messages with `simdjson` ondemand APIs
- Normalizes exchange messages into typed `MarketEvent` objects
- Moves data between pipeline stages with custom bounded SPSC ring buffers
- Uses `std::jthread` workers for feed, parser, order book, strategy, and
  telemetry stages
- Applies price-level updates into an in-memory order book
- Tracks active price levels with packed `uint64_t` bitmaps
- Maintains cached top-N bid and ask levels
- Computes market microstructure features including spread, mid price,
  microprice, imbalance, and order-flow imbalance
- Emits rule-based strategy decisions:
  `STRONG_BUY`, `BUY`, `WAIT`, `SELL`, or `STRONG_SELL`
- Carries metadata through the pipeline for latency instrumentation
- Includes an early AMQP/RabbitMQ publisher path for downstream telemetry

Still in progress:

- Kraken checksum validation and gap handling
- Reconnect and resubscription behavior
- Structured telemetry serialization
- Replayable market-data tests
- Unit tests and microbenchmarks
- Cleaner shutdown behavior for long-running worker threads
- Risk module and execution path

## Architecture

Current pipeline:

```text
Kraken WebSocket
    -> ExchangeMessage { metadata, raw JSON }
    -> KrakenSpscRing
    -> simdjson parser
    -> MarketEvent { metadata, bids, asks, type }
    -> OrderBookRing
    -> OrderBook update path
    -> FeatureBook { metadata, microstructure features }
    -> FeatureBookRing
    -> StrategyEvent { metadata, order decision }
    -> StrategyRing
    -> Telemetry / future risk path
```

## Core Components

### Feed Client

Connects to Kraken's WebSocket endpoint using Boost.Asio/Beast over OpenSSL.
Incoming messages are timestamped locally and published into a bounded SPSC
queue.

Relevant files:

- `include/feed/feed.h`
- `src/feed/feed.cpp`

### Parser

Uses `simdjson` ondemand APIs to extract book-channel messages and convert them
into typed `MarketEvent` objects.

Relevant files:

- `include/feed/parser.h`
- `src/feed/parser.cpp`
- `include/common/time_utils.h`
- `src/common/time_utils.cpp`

### SPSC Ring Buffer

The pipeline uses custom single-producer/single-consumer ring buffers between
stages. The ring uses fixed capacity, atomic indexes, acquire/release memory
ordering, cache-line alignment, and move semantics.

Relevant file:

- `include/templates/spsc_ring.h`

### Metadata

`Metadata` is the timing and identity object carried through the pipeline. It
tracks exchange timestamps, local receive timestamps, local stage-completion
timestamps, message id, and symbol.

Relevant files:

- `include/common/metadata.h`
- `src/common/metadata.cpp`

### Order Book

The order book stores price levels in fixed-size arrays indexed by normalized
tick price. Active levels are tracked with packed bitmaps, and top-N bid/ask
levels are cached for feature calculation.

Relevant files:

- `include/order-book/order_book.h`
- `src/order_book/order_book.cpp`

### Features

The feature layer converts current order book state into a `FeatureBook`.
Currently calculated features include top bid/ask, quantities, spread, tick
spread, mid price, microprice, microprice edge, top-level imbalance, top-N
imbalance, top-level OFI, and top-N OFI.

Relevant files:

- `include/order-book/features.h`
- `src/order_book/features.cpp`

### Strategy

The strategy layer consumes `FeatureBook` and emits a `StrategyEvent`. The
current strategy is intentionally rule-based and interpretable.

Relevant files:

- `include/strategy/strategy.h`
- `src/strategy/strategy.cpp`

### Telemetry Export

The project includes an early AMQP publisher path intended for future structured
telemetry delivery.

Relevant files:

- `include/common/amqp_publisher.h`
- `include/common/metadata.h`

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio / Boost.Beast
- OpenSSL
- simdjson
- AMQP-CPP
- RabbitMQ

## Build

Prerequisites:

- C++23-capable compiler
- CMake
- Conan
- RabbitMQ if running the AMQP telemetry path

Configure and build:

```bash
cmake --preset debug
cmake --build "$HOME/.cmake-build/trading-infrastructure/debug"
```

Run:

```bash
"$HOME/.cmake-build/trading-infrastructure/debug/trading_infrastructure"
```

Release build:

```bash
cmake --preset release
cmake --build "$HOME/.cmake-build/trading-infrastructure/release"
```

CLion should use the root `CMakeLists.txt` and run the
`trading_infrastructure` CMake target. Do not run `src/main.cpp` as a standalone
file; that bypasses CMake, Conan, include paths, source files, and link
libraries.

## Docker

Build the container:

```bash
docker build -t trading-infrastructure .
```

Run it:

```bash
docker run --rm trading-infrastructure
```

Railway can deploy this repo directly from GitHub using the root `Dockerfile`.
This currently runs as a worker-style process, not an HTTP web service, so the
useful output is in the Railway logs unless a web server is added later.
