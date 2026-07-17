# Orderbook

A C++23 real-time order book pipeline for live Kraken BTC/USD market data.

The backend ingests Kraken WebSocket book updates, parses messages with
`simdjson`, applies price-level updates to an in-memory order book, computes
market microstructure features, emits rule-based strategy decisions, and exports
telemetry over WebSocket to a dashboard.

This is a systems project, not production trading software. It should not be
used to place orders or make financial decisions.

## Highlights

- Live Kraken WebSocket v2 ingestion over TLS with Boost.Asio and Boost.Beast
- `simdjson` ondemand parsing into typed market events
- Staged `std::jthread` pipeline connected by custom bounded SPSC ring buffers
- Fixed-array order book indexed by normalized tick price
- Packed `uint64_t` bitmaps for active price-level tracking
- Cached top-N bid/ask levels for fast feature calculation and telemetry
- Microstructure features: spread, tick spread, midprice, microprice,
  microprice edge, top-level imbalance, top-N imbalance, and order-flow
  imbalance
- Rule-based strategy decisions: `STRONG_BUY`, `BUY`, `WAIT`, `SELL`,
  `STRONG_SELL`
- Direct C++ to dashboard telemetry over WebSocket, without RabbitMQ or another
  message broker
- React/TypeScript dashboard for live depth, features, decisions, and rolling
  latency metrics

## Architecture

```text
Kraken WebSocket
    -> ExchangeMessage { telemetry, raw JSON }
    -> KrakenSpscRing
    -> simdjson parser
    -> MarketEvent { telemetry, bids, asks, type }
    -> OrderBookRing
    -> OrderBook update
    -> FeatureBook { telemetry, microstructure features }
    -> FeatureBookRing
    -> StrategyEvent { telemetry, decision }
    -> TelemetrySpscRing
    -> WebSocket telemetry publisher
    -> Dashboard ingest server
```

## Repository Layout

```text
include/      C++ headers
src/          C++ source files
dashboard/    React dashboard and Node.js ingest/SSE server
```

Important files:

```text
include/templates/spsc_ring.h          Custom SPSC ring buffer
include/common/telemetry.h             Telemetry payload structs
include/common/websocket_publisher.h   C++ telemetry WebSocket publisher
src/feed/feed.cpp                      Kraken WebSocket client
src/feed/parser.cpp                    simdjson Kraken book parser
src/order_book/order_book.cpp          Bitmap-backed order book
src/order_book/features.cpp            Microstructure feature calculation
src/strategy/strategy.cpp              Rule-based strategy decisions
src/common/telemetry.cpp               Telemetry JSON serialization
dashboard/server/index.ts              WebSocket ingest + HTTP/SSE server
dashboard/src/App.tsx                  Dashboard UI
```

## Core Components

### Feed

Connects to Kraken's public WebSocket v2 API over TLS, subscribes to live
BTC/USD book updates, timestamps each received message, and publishes raw JSON
into a bounded SPSC queue.

### Parser

Uses `simdjson` ondemand APIs to filter Kraken `book` channel messages and
normalize bid/ask updates into typed `MarketEvent` objects.

### SPSC Ring Buffer

The pipeline uses custom single-producer/single-consumer ring buffers between
stages. The ring uses fixed power-of-two capacity, atomic read/write indexes,
acquire/release memory ordering, cache-line alignment, and move semantics.

### Order Book

The order book stores quantities in fixed-size arrays indexed by normalized
tick price. Active levels are tracked with packed bitmaps, allowing the update
path to maintain cached top-N bid and ask levels without scanning the full book
on every telemetry export.

### Features

The feature layer computes market microstructure values from the current top-N
book:

- Top bid/ask and quantities
- Spread and tick spread
- Midprice
- Microprice
- Microprice edge in ticks
- Top-level imbalance
- Top-N imbalance
- Top-level OFI
- Top-N OFI

### Strategy

The strategy layer is intentionally simple and interpretable. It consumes the
feature book and emits one of:

```text
STRONG_BUY
BUY
WAIT
SELL
STRONG_SELL
```

### Telemetry

Telemetry is carried through the pipeline and serialized as a nested JSON
object containing:

- Identifier data
- Stage latency metrics
- Top-N order book levels
- Feature values
- Strategy decision

The C++ worker publishes telemetry directly to the dashboard using a persistent
WebSocket connection.

### Dashboard

The dashboard includes:

- Live order book depth chart
- Bid/ask depth table
- Decision history timeline
- Current feature values
- P99 and P99.9 total local latency charts
- Rolling latency table with min, p50, mean, p99, p99.9, max, and standard
  deviation

The Node.js server accepts C++ telemetry on a WebSocket ingest endpoint, serves
the React build, and streams browser updates over Server-Sent Events.

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio
- Boost.Beast
- OpenSSL
- simdjson
- TypeScript
- React
- Node.js
- WebSocket
- Server-Sent Events
- Docker
- Railway

## Build

Prerequisites:

- C++23-capable compiler
- CMake
- Conan
- Node.js 20+ for the dashboard

Configure and build the C++ worker:

```bash
cmake --preset debug
cmake --build "$HOME/.cmake-build/trading-infrastructure/debug"
```

Run the C++ worker:

```bash
METRICS_WS_URL=ws://127.0.0.1:3000/ingest \
"$HOME/.cmake-build/trading-infrastructure/debug/trading_infrastructure"
```

Do not run `src/main.cpp` as a standalone file. Use the CMake target so Conan
dependencies, include paths, source files, and link libraries are configured
correctly.

## Dashboard

Build and run the dashboard:

```bash
cd dashboard
npm install
npm run build
PORT=3000 npm start
```

Useful dashboard variables:

```text
PORT=3000
HOST=0.0.0.0
METRICS_INGEST_PATH=/ingest
WINDOW_SIZE=10000
REFRESH_MS=1000
INGEST_BATCH_SIZE=1000
DECISION_HISTORY_SIZE=240
```

Run the C++ worker against the local dashboard:

```bash
METRICS_WS_URL=ws://127.0.0.1:3000/ingest \
"$HOME/.cmake-build/trading-infrastructure/debug/trading_infrastructure"
```

## Docker

Build the C++ worker image:

```bash
docker build -f Dockerfile-app -t trading-infrastructure-app .
```

Build the dashboard image:

```bash
docker build -f Dockerfile-dashboard -t trading-dashboard .
```

Run the dashboard:

```bash
docker run --rm -p 3000:3000 trading-dashboard
```

Run the worker against the dashboard:

```bash
docker run --rm \
  -e METRICS_WS_URL=ws://host.docker.internal:3000/ingest \
  trading-infrastructure-app
```

## Railway

Use two Railway services:

- `trading-infrastructure-dashboard`
- `trading-infrastructure-app`

Dashboard variables:

```text
PORT=3000
HOST=::
METRICS_INGEST_PATH=/ingest
```

App variable:

```text
METRICS_WS_URL=ws://<dashboard-private-domain>.railway.internal:3000/ingest
```

Use the dashboard service's `RAILWAY_PRIVATE_DOMAIN`, not the app service's
private domain. The dashboard is the WebSocket receiver; the C++ app is the
publisher.

## Current Limitations

- Kraken checksum validation and gap recovery are not implemented yet
- Replayable market-data tests are still needed
- Unit tests and microbenchmarks are still needed
- Shutdown behavior is intentionally simple for long-running worker threads
- The strategy is rule-based and meant for inspection, not execution
- There is no risk module or order execution path
