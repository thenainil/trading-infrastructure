# Trading Infrastructure

A C++23 trading-infrastructure project focused on real-time market-data
ingestion, low-overhead inter-thread handoff, order book construction,
microstructure feature calculation, strategy signaling, and telemetry export.

This is not production trading software and should not be used to place orders
or make financial decisions. The goal is to build an inspectable systems project
that exercises the mechanics behind latency-sensitive trading infrastructure.

## Repository Layout

```text
engine/   C++ engine, CMake, Conan, feed handling, order book, features, strategy
scripts/  Python service stub for future telemetry consumption
web/      React/TypeScript dashboard stub
```

The C++ engine is the core of the project. The Python and TypeScript pieces are
currently scaffolding for the future telemetry/dashboard path.

## Current Status

Implemented in the C++ engine:

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
- Proper Python telemetry consumer
- Real dashboard visualizations
- Replayable market-data tests
- Unit tests and microbenchmarks
- Cleaner shutdown behavior for long-running worker threads
- Risk module and execution path

## Architecture

Current C++ pipeline:

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

The design keeps the market-data hot path in C++ and pushes observability toward
external consumers. The intended long-term flow is:

```text
C++ engine -> RabbitMQ telemetry -> Python aggregation -> TypeScript dashboard
```

## Core Engine Components

### Feed Client

Connects to Kraken's WebSocket endpoint using Boost.Asio/Beast over OpenSSL.
Incoming messages are timestamped locally and published into a bounded SPSC
queue.

Relevant files:

- `engine/include/feed/feed.h`
- `engine/src/feed/feed.cpp`

### Parser

Uses `simdjson` ondemand APIs to extract book-channel messages and convert them
into typed `MarketEvent` objects.

Relevant files:

- `engine/include/feed/parser.h`
- `engine/src/feed/parser.cpp`
- `engine/include/common/time_utils.h`
- `engine/src/common/time_utils.cpp`

### SPSC Ring Buffer

The pipeline uses custom single-producer/single-consumer ring buffers between
stages. The ring uses fixed capacity, atomic indexes, acquire/release memory
ordering, cache-line alignment, and move semantics.

Relevant file:

- `engine/include/templates/spsc_ring.h`

### Metadata

`Metadata` is the timing and identity object carried through the pipeline. It
tracks exchange timestamps, local receive timestamps, local stage-completion
timestamps, message id, and symbol.

Relevant files:

- `engine/include/common/metadata.h`
- `engine/src/common/metadata.cpp`

### Order Book

The order book stores price levels in fixed-size arrays indexed by normalized
tick price. Active levels are tracked with packed bitmaps, and top-N bid/ask
levels are cached for feature calculation.

Relevant files:

- `engine/include/order-book/order_book.h`
- `engine/src/order_book/order_book.cpp`

### Features

The feature layer converts current order book state into a `FeatureBook`.
Currently calculated features include top bid/ask, quantities, spread, tick
spread, mid price, microprice, microprice edge, top-level imbalance, top-N
imbalance, top-level OFI, and top-N OFI.

Relevant files:

- `engine/include/order-book/features.h`
- `engine/src/order_book/features.cpp`

### Strategy

The strategy layer consumes `FeatureBook` and emits a `StrategyEvent`. The
current strategy is intentionally rule-based and interpretable.

Relevant files:

- `engine/include/strategy/strategy.h`
- `engine/src/strategy/strategy.cpp`

### Telemetry Export

The engine includes an early AMQP publisher path intended for future structured
telemetry delivery. The current Python and TypeScript apps are stubs around that
future direction.

Relevant files:

- `engine/include/common/amqp_publisher.h`
- `engine/include/common/metadata.h`
- `scripts/hello_world.py`
- `web/src/App.tsx`

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio / Boost.Beast
- OpenSSL
- simdjson
- AMQP-CPP
- RabbitMQ
- Python
- React / TypeScript / Vite

## Build The C++ Engine

Prerequisites:

- C++23-capable compiler
- CMake
- Conan
- RabbitMQ if running the AMQP telemetry path

Configure and build from the `engine/` package:

```bash
cd engine
cmake --preset conan
cmake --build "$HOME/.cmake-build/trading-infrastructure/engine/conan"
```

Run:

```bash
"$HOME/.cmake-build/trading-infrastructure/engine/conan/trading_infrastructure"
```

Release build:

```bash
cd engine
cmake --preset release
cmake --build "$HOME/.cmake-build/trading-infrastructure/engine/release"
```

CLion should use `engine/CMakeLists.txt` as the CMake project and run the
`trading_infrastructure` CMake target. Do not run `engine/src/main.cpp` as a
standalone file; that bypasses CMake, Conan, include paths, source files, and
link libraries.

## Run The Stub Services

Python stub:

```bash
python3 scripts/hello_world.py
```

Web stub:

```bash
cd web
npm install
npm run dev
```

The Vite dev server runs on:

```text
http://localhost:9000
```

## Known Risks And Open Issues

- The system currently uses Kraken public WebSocket data, so network latency is
  dominated by public Internet routing and exchange-side timing.
- Exchange-to-local latency depends on wall-clock synchronization.
- Local stage latency should use `std::chrono::steady_clock`.
- Debug output in the hot path will distort latency measurements.
- Kraken checksum validation is not complete.
- Gap detection and reconnect recovery are not complete.
- Depth truncation and out-of-window book updates need stronger handling.
- Fixed-size price arrays need bounds checks or resync behavior if market price
  moves outside the supported range.
- The strategy is an interpretable rule engine, not a validated profitable
  trading strategy.
- Risk, execution, order management, and PnL accounting are not implemented.
- TLS peer verification is not production-hardened yet.

## Project Direction

Planned improvements:

- Implement Kraken checksum validation
- Add sequence/gap detection and reconnect logic
- Add robust snapshot/resync behavior
- Add replayable market-data tests
- Add unit tests and microbenchmarks
- Publish structured telemetry over RabbitMQ
- Build a real telemetry aggregation service
- Build a live dashboard for latency, market state, features, and decisions
- Add rolling statistical features and z-score filters
- Add spread-aware risk checks before any execution path

## Notes

Generated build directories, IDE files, dependency folders, and compiled
binaries should not be committed. GitHub language statistics are adjusted with
`.gitattributes` so third-party CMake/Conan glue does not dominate the language
breakdown.
