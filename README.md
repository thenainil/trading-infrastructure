# Trading Infrastructure

A C++23 market data infrastructure project focused on real-time feed ingestion,
low-overhead inter-thread handoff, timestamp handling, and order book
construction.

This is my flagship C++ systems project as I transition from backend Java work
into lower-level, latency-sensitive engineering. The goal is not to present this
as production trading software. The goal is to build and document the core
mechanics behind a market data pipeline: asynchronous WebSocket ingestion,
structured parsing, bounded queues, local order book state, and telemetry export.

## Current Status

This project is a work in progress and should not be used to place orders or
make financial decisions.

Implemented so far:

- Connects to Kraken's public WebSocket v2 API over TLS
- Subscribes to live BTC/USD book updates
- Uses Boost.Asio and Boost.Beast for asynchronous WebSocket I/O
- Parses Kraken book messages with `simdjson` ondemand APIs
- Normalizes exchange messages into typed `MarketEvent` objects
- Moves data between pipeline stages with a custom bounded SPSC ring buffer
- Uses `std::jthread` workers for feed, parser, order book, and telemetry stages
- Converts Kraken ISO-8601 UTC timestamps into C++ chrono time points
- Applies price-level updates into an in-memory order book
- Tracks active price levels with packed `uint64_t` bitmaps
- Resolves best bid and best ask with bitmap scans and C++ bit operations
- Includes an early AMQP/RabbitMQ publisher path for downstream dashboards
- Builds with CMake, Conan, Boost, OpenSSL, simdjson, and AMQP-CPP

Still in progress:

- Complete `BookEvent` emission with populated metadata and top-N levels
- Correct Kraken depth truncation semantics for maintaining top-N book state
- Kraken checksum validation and gap handling
- Reconnect and resubscription behavior
- Structured telemetry serialization for Python/TypeScript consumers
- Unit tests, replay tests, and benchmarks
- Cleaner shutdown behavior for long-running worker threads
- TLS peer verification hardening

## Architecture

The current data flow is:

```text
Kraken WebSocket
    -> ExchangeMessage { receive timestamp, raw JSON }
    -> SPSC ring buffer
    -> simdjson parser
    -> MarketEvent
    -> SPSC ring buffer
    -> OrderBook update path
    -> BookEvent / Telemetry
    -> SPSC ring buffer
    -> AMQP/RabbitMQ publisher
```

The design separates the market-data hot path from downstream observability.
The C++ process handles ingestion, parsing, order book mutation, and event
publication. Python and TypeScript consumers can subscribe through RabbitMQ for
logging, plotting, dashboards, and experimentation without blocking the core
pipeline.

## Core Components

### Feed Client

The feed client connects to Kraken's WebSocket v2 endpoint using
Boost.Asio/Beast over OpenSSL. Incoming messages are timestamped locally and
published into a bounded SPSC queue as raw exchange messages.

Relevant files:

- `include/feed/feed.h`
- `src/feed/feed.cpp`

### Parser

The parser uses `simdjson` ondemand APIs to extract book channel messages,
including symbol, side, price levels, quantities, message type, and exchange
timestamp.

Relevant files:

- `include/feed/parser.h`
- `src/feed/parser.cpp`
- `include/common/time_utils.h`
- `src/common/time_utils.cpp`

### SPSC Ring Buffer

The pipeline uses a custom single-producer/single-consumer ring buffer between
stages. The ring uses:

- Fixed power-of-two capacity
- Masking instead of modulo for wraparound
- `std::atomic<std::size_t>` indexes
- Acquire/release memory ordering
- Move semantics for payload transfer
- Cache-line alignment for producer/consumer indexes

Relevant file:

- `include/templates/spsc_ring.h`

### Order Book

The order book stores price levels in fixed-size arrays indexed by normalized
tick price. Active levels are tracked separately with packed `uint64_t` bitmaps.
This allows best-price discovery to scan 64 price levels at a time and then use
CPU bit operations to resolve the exact price index.

Best ask lookup scans ask bitmaps forward and uses `std::countr_zero`.
Best bid lookup scans bid bitmaps backward and uses `std::countl_zero`.

Relevant files:

- `include/order-book/order_book.h`
- `src/order_book/order_book.cpp`

### Telemetry Export

The project includes an early AMQP publishing path intended to send book events
and pipeline telemetry to external consumers. The intended downstream setup is a
Python or TypeScript dashboard that can log live events, visualize spread and
mid price, and display per-stage latency metrics.

Relevant files:

- `include/common/telemetry.h`
- `src/common/telemetry.cpp`
- `include/common/amqp_publisher.h`

## Low-Latency Concepts Demonstrated

This repository is intentionally focused on fundamentals that matter in
latency-sensitive C++ systems:

- Bounded queues instead of unbounded shared containers
- Single-writer/single-reader ownership between pipeline stages
- Atomic memory ordering instead of mutex-based handoff
- Cache-line awareness around frequently written indexes
- Fixed-size arrays for predictable order book storage
- Packed bitmaps for compact active-level tracking
- Bit operations for best-price discovery
- Separation between hot-path processing and telemetry/export work
- Explicit exchange timestamps and local receive timestamps

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio
- Boost.Beast
- OpenSSL
- simdjson
- AMQP-CPP
- `std::jthread`
- Custom SPSC ring buffer

## Build

Prerequisites:

- A C++23-capable compiler
- CMake
- Conan
- RabbitMQ if running the AMQP telemetry path

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

## Resume Summary

This project can be summarized as:

```text
Built a C++23 real-time market data pipeline consuming Kraken WebSocket order
book updates, parsing messages with simdjson, moving events through custom SPSC
queues, maintaining a bitmap-backed price-level order book, and exporting
telemetry for external Python/TypeScript visualization.
```

Key resume points:

- Built a live Kraken WebSocket market data pipeline with asynchronous TLS
  ingestion, `simdjson` ondemand parsing, staged SPSC ring-buffer handoff, and
  an in-memory BTC/USD order book.
- Implemented a custom lock-free SPSC queue using power-of-two masking,
  acquire/release atomics, move semantics, and cache-line-aligned indexes for
  low-overhead inter-thread communication.
- Designed a bitmap-backed price-level book using fixed arrays plus packed
  `uint64_t` active-level masks, resolving best bid/ask via
  `countl_zero`/`countr_zero` instead of heap or tree traversal.
- Added exchange timestamp parsing and an AMQP telemetry path for exporting
  live book state and latency data to Python/TypeScript visualization
  dashboards.

## Project Goals

The long-term goal is to build a small but serious trading infrastructure system
that demonstrates:

- Low-latency C++ design
- Market data feed handling
- Order book construction
- Event-driven architecture
- Concurrency and memory-layout awareness
- Testing, profiling, and benchmarking discipline
- Observability through external dashboards

Planned improvements:

- Implement Kraken checksum validation for book correctness
- Add sequence/gap detection and reconnect logic
- Add top-N book snapshots with proper depth truncation
- Add replayable market data tests
- Add microbenchmarks for parsing, queue handoff, and order book updates
- Publish structured JSON or binary telemetry over RabbitMQ
- Build a Python/TypeScript dashboard for live visualization

## Notes

The project currently disables TLS peer verification in the WebSocket client
while the infrastructure is being built out. That should be changed before
treating the feed client as production-ready.

Generated build directories, IDE files, and compiled binaries should not be
committed.
