# Trading Infrastructure

A C++23 trading-infrastructure project focused on real-time market-data
ingestion, low-overhead inter-thread handoff, order book construction,
microstructure feature calculation, strategy signaling, and telemetry export.

This is not production trading software and should not be used to place orders
or make financial decisions. The goal is to build an inspectable systems project
that exercises the mechanics behind latency-sensitive trading infrastructure.

## Project Status

This repository is being left as a C++ prototype / reference implementation.
Future development is moving to a Java implementation, likely with this repo
serving as the systems and latency-learning baseline.

The current C++ code is still useful for:

- Kraken WebSocket ingestion experiments
- parser/order-book latency measurement
- RabbitMQ telemetry publishing
- the React metrics dashboard
- reference implementations of the ring buffer, order book, features, and
  staged thread pipeline

## Repository Layout

```text
include/  Public headers
src/      C++ source files
dashboard/ React metrics dashboard and AMQP-backed HTTP server
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
- Includes a React dashboard that consumes RabbitMQ latency metrics over a
  small Node.js HTTP/SSE server

Still in progress:

- Kraken checksum validation and gap handling
- Reconnect and resubscription behavior
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

The project includes an AMQP publisher path for latency telemetry. The C++
worker publishes JSON metrics to RabbitMQ using the `trade_metrics` routing key.

Relevant files:

- `include/common/amqp_publisher.h`
- `include/common/metadata.h`
- `src/common/metadata.cpp`

### Metrics Dashboard

The `dashboard/` directory contains a minimal React dashboard. A small Node.js
server consumes the RabbitMQ `trade_metrics` queue, serves the React build,
and streams rolling min, p50, p99, p99.9, and max latency values to the browser
over server-sent events.

Relevant files:

- `dashboard/server/index.ts`
- `dashboard/src/App.tsx`
- `dashboard/package.json`

Local dashboard run:

```bash
cd dashboard
npm install
npm run build
PORT=3000 npm start
```

Railway deployments use `Dockerfile-app` for the C++ worker and
`Dockerfile-dashboard` for the dashboard. Set `RABBITMQ_URL` and, if needed,
`QUEUE_NAME`; Railway provides `PORT` for the dashboard.

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio / Boost.Beast
- OpenSSL
- simdjson
- AMQP-CPP
- RabbitMQ
- Node.js 20+ for the dashboard

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

## RabbitMQ and Dashboard

Start RabbitMQ locally:

```bash
docker run --rm -it \
  --name trading-rabbitmq \
  -p 5672:5672 \
  -p 15672:15672 \
  rabbitmq:3-management
```

RabbitMQ management UI:

```text
http://localhost:15672
user: guest
pass: guest
```

The dashboard passively checks for an existing `trade_metrics` queue. Start the
C++ worker first, or create the queue manually in the RabbitMQ UI.

Run the dashboard:

```bash
cd dashboard
npm install
npm start
```

Optional dashboard configuration:

```bash
RABBITMQ_URL=amqp://localhost:5672 \
QUEUE_NAME=trade_metrics \
WINDOW_SIZE=10000 \
REFRESH_MS=1000 \
npm start
```

## Docker

Build the C++ worker container:

```bash
docker build -f Dockerfile-app -t trading-infrastructure-app .
```

Run it against local RabbitMQ on Docker Desktop:

```bash
docker run --rm \
  -e RABBITMQ_URL=amqp://host.docker.internal:5672 \
  trading-infrastructure-app
```

Run it against cloud RabbitMQ by setting `RABBITMQ_URL` to the `amqps://...`
connection string.

Build the dashboard container:

```bash
docker build -f Dockerfile-dashboard -t trading-dashboard .
```

Run the dashboard container against local RabbitMQ on Linux:

```bash
docker run --rm --network host trading-dashboard
```

On Docker Desktop for macOS, use `host.docker.internal` for RabbitMQ:

```bash
docker run --rm \
  -e RABBITMQ_URL=amqp://host.docker.internal:5672 \
  trading-dashboard
```
