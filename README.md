# Trading Infrastructure

A C++23 market-data infrastructure project focused on real-time WebSocket feed
ingestion, low-overhead inter-thread handoff, order book construction,
microstructure feature calculation, strategy signaling, and telemetry export.

This is my flagship C++ systems project as I transition from backend Java work
into lower-level, latency-sensitive engineering. I work in finance, but this
project is about learning and building the mechanics behind trading systems from
the ground up: asynchronous network ingestion, parsing, bounded queues, local
book state, feature extraction, strategy/risk decisions, and external
observability.

The goal is not to present this as production trading software. The goal is to
build a serious, inspectable research and infrastructure environment where each
piece of the trading pipeline can be measured, improved, and eventually
visualized live.

## Current Status

This project is a work in progress and should not be used to place orders or
make financial decisions.

Implemented so far:

- Connects to Kraken's public WebSocket v2 API over TLS
- Subscribes to live BTC/USD book updates
- Uses Boost.Asio and Boost.Beast for asynchronous WebSocket I/O
- Timestamps exchange messages at local receive time
- Parses Kraken book messages with `simdjson` ondemand APIs
- Normalizes exchange messages into typed `MarketEvent` objects
- Moves data between pipeline stages with custom bounded SPSC ring buffers
- Uses `std::jthread` workers for feed, parser, order book, strategy, and
  telemetry stages
- Converts Kraken ISO-8601 UTC timestamps into C++ chrono time points
- Applies price-level updates into an in-memory order book
- Tracks active price levels with packed `uint64_t` bitmaps
- Maintains cached top-N bid and ask levels as `BookLevel` objects
- Resolves top-N bid/ask levels with C++ bit operations:
  `std::countl_zero` for bids and `std::countr_zero` for asks
- Computes point-in-time market microstructure features:
  top bid/ask, quantities, spread, tick spread, mid price, microprice,
  microprice edge, top-level imbalance, top-N imbalance, top-level OFI, and
  top-N OFI
- Produces a rule-based strategy decision:
  `STRONG_BUY`, `BUY`, `WAIT`, `SELL`, or `STRONG_SELL`
- Instruments pipeline latency by stage:
  network, parse, order book update, feature calculation, and strategy decision
- Includes an early AMQP/RabbitMQ publisher path for downstream dashboards

Still in progress:

- Correct Kraken checksum validation and gap handling
- Reconnect and resubscription behavior
- Structured telemetry serialization for Python/TypeScript consumers
- Dashboard for live latency, market state, feature values, strategy decisions,
  and later PnL/risk visualization
- More rigorous order book recovery behavior when price levels fall outside the
  cached depth window
- More careful handling of Kraken depth truncation semantics
- Replayable market-data tests
- Unit tests for parser, order book, features, and strategy logic
- Microbenchmarks for parsing, queue handoff, book mutation, top-N lookup,
  feature calculation, and strategy evaluation
- Cleaner shutdown behavior for long-running worker threads
- TLS peer verification hardening
- Risk module with spread-aware take-profit and stop-loss logic
- Statistical filter layer using rolling z-scores, rolling volatility, and
  regression-style trend estimates

## Architecture

The current data flow is:

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
    -> Telemetry / dashboard / future risk and execution path
```

The design separates the market-data hot path from downstream observability.
The C++ process handles ingestion, parsing, order book mutation, feature
calculation, and strategy signaling. Python and TypeScript consumers are
intended to subscribe through RabbitMQ for logging, plotting, dashboards,
statistical analysis, and experimentation without blocking the core pipeline.

The long-term operating loop is:

```text
Measure live latency -> visualize it -> improve one subsystem -> verify the
latency distribution improved -> repeat.
```

That turns C++ performance work into a visible feedback loop instead of a black
box benchmark.

## Core Components

### Feed Client

The feed client connects to Kraken's WebSocket v2 endpoint using
Boost.Asio/Beast over OpenSSL. Incoming messages are timestamped locally and
published into a bounded SPSC queue as raw exchange messages.

Relevant files:

- `include/feed/feed.h`
- `src/feed/feed.cpp`

### Parser

The parser uses `simdjson` ondemand APIs to extract book-channel messages,
including symbol, side, price levels, quantities, message type, and exchange
timestamp.

Parsed messages become typed `MarketEvent` objects. The parser carries metadata
forward so later stages can measure parse latency, book latency, feature
latency, and strategy latency from one common timing object.

Relevant files:

- `include/feed/parser.h`
- `src/feed/parser.cpp`
- `include/common/time_utils.h`
- `src/common/time_utils.cpp`

### SPSC Ring Buffer

The pipeline uses custom single-producer/single-consumer ring buffers between
stages. The ring uses:

- Fixed power-of-two capacity
- Masking instead of modulo for wraparound
- `std::atomic<std::size_t>` indexes
- Acquire/release memory ordering
- Move semantics for payload transfer
- Cache-line alignment for producer/consumer indexes

The ring buffer is intentionally simple and specialized. It is not a general
multi-producer queue; it is built for the pipeline shape used here, where each
handoff has exactly one producing thread and one consuming thread.

Relevant file:

- `include/templates/spsc_ring.h`

### Metadata

`Metadata` is the timing and identity object that flows through the pipeline.
It currently tracks:

- Exchange timestamp
- Local wall-clock receive timestamp
- Local monotonic receive timestamp
- Parse-complete timestamp
- Order-book-complete timestamp
- Feature-calculation-complete timestamp
- Strategy-decision-complete timestamp
- Monotonic message id
- Symbol

Both wall-clock and monotonic timestamps are used because they answer different
questions:

- Wall-clock time is needed for exchange-to-local latency comparisons.
- `steady_clock` time is needed for stable local stage-to-stage latency
  measurement.

If network latency appears as a negative number, that usually means the local
wall clock and exchange timestamp are not comparable at that moment. The local
system clock may be behind the exchange timestamp, the exchange timestamp may
represent a later event time than expected, or timestamp fields may be
mismatched. Local stage latency should use `steady_clock`; exchange-to-local
latency necessarily depends on wall-clock synchronization.

Relevant file:

- `include/common/metadata.h`

### Order Book

The order book stores price levels in fixed-size arrays indexed by normalized
tick price. Active levels are tracked separately with packed `uint64_t` bitmaps.
This gives predictable storage and avoids map/tree traversal for the hot path.

Current shape:

- `ask_order_book[index]` stores ask quantity at a normalized price index
- `bid_order_book[index]` stores bid quantity at a normalized price index
- `ask_bit_map[word]` stores active ask levels, 64 prices per word
- `bid_bit_map[word]` stores active bid levels, 64 prices per word
- `top_n_asks` stores cached top-N ask `BookLevel`s
- `top_n_bids` stores cached top-N bid `BookLevel`s
- `top_ask` and `top_bid` store the current best levels

Top-N lookup starts near the current best price and scans outward:

- Bids scan downward from best bid and use `std::countl_zero` to find the
  highest active bit in each word.
- Asks scan upward from best ask and use `std::countr_zero` to find the lowest
  active bit in each word.

This keeps the order book as a state object. Calculations such as microprice,
imbalance, OFI, and future statistical features belong in the feature layer.

Relevant files:

- `include/order-book/order_book.h`
- `src/order_book/order_book.cpp`

### Features

The feature layer converts the current order book state into a `FeatureBook`.
It intentionally performs calculations outside the order book so the book
remains a compact market-state object.

Currently calculated features:

- Top bid
- Top ask
- Top bid quantity
- Top ask quantity
- Spread
- Tick spread
- Mid price
- Microprice
- Microprice edge in ticks
- Top-level imbalance
- Top-N imbalance
- Top-level order flow imbalance
- Top-N order flow imbalance

The feature layer keeps previous top-N bid/ask snapshots so OFI can compare the
current top-of-book state to the previous observed state.

Relevant files:

- `include/order-book/features.h`
- `src/order_book/features.cpp`

### Strategy

The strategy layer consumes `FeatureBook` and emits a `StrategyEvent`.

The current strategy is intentionally rule-based and interpretable. It gates
trades on spread, microprice edge, imbalance, OFI, and relative top-level
quantity. It emits:

- `STRONG_BUY`
- `BUY`
- `WAIT`
- `SELL`
- `STRONG_SELL`

Example strategy intuition:

- Bullish pressure: microprice above mid, positive microprice edge, positive
  imbalance, positive OFI, and bid-side quantity stronger than ask-side
  quantity.
- Bearish pressure: microprice below mid, negative microprice edge, negative
  imbalance, negative OFI, and ask-side quantity stronger than bid-side
  quantity.
- Spread filter: only act when the spread is small enough that the signal has a
  chance to survive transaction costs and crossing the spread.

Relevant files:

- `include/strategy/strategy.h`
- `src/strategy/strategy.cpp`

### Telemetry Export

The project includes an early AMQP publishing path intended to send book events,
feature values, strategy decisions, and pipeline telemetry to external
consumers.

The intended downstream setup is a Python or TypeScript dashboard that can:

- Display live network latency
- Display parser, order book, feature, and strategy latency
- Plot mid price, microprice, spread, and imbalance
- Show strategy decisions in real time
- Later overlay trade entries, exits, stops, take-profits, and PnL
- Track whether C++ changes produce visible latency improvements

Relevant files:

- `include/common/metadata.h`
- `src/common/metadata.cpp`
- `include/common/amqp_publisher.h`

## Latency Measurement

The project currently prints per-message latency like:

```text
Network Latency: 39624us
Parse Latency: 10584ns
Order Book Latency: 1500ns
Feature Latency: 84ns
Strategy Latency: 42ns
```

The important distinction is:

- Network latency is exchange wall-clock timestamp to local wall-clock receive
  timestamp. On a retail public WebSocket feed this can easily be tens of
  milliseconds.
- Parse latency is local receive to parse complete.
- Order book latency is parse complete to book update complete.
- Feature latency is book update complete to feature calculation complete.
- Strategy latency is feature calculation complete to strategy decision
  complete.

Local stage latency should be measured with `std::chrono::steady_clock`.
Exchange-to-local latency must use wall-clock time and is only as accurate as
clock synchronization and the exchange timestamp semantics.

Observed development measurements have shown:

- Network latency dominates the pipeline on a public retail WebSocket feed.
- The C++ local path can operate in nanoseconds to low microseconds for many
  messages once debug printing and unnecessary calculations are removed.
- `std::cout` in the hot path is useful while learning, but it distorts
  latency measurements and should eventually move to telemetry sampling or a
  dedicated output path.
- Expensive feature logic or unnecessary top-N scans can dominate local
  latency, so feature calculation needs the same measurement discipline as the
  order book.

## Statistical Filter And Risk Direction

The next major direction is adding a statistical filter between raw
microstructure features and strategy/risk decisions.

Current strategy shape:

```text
OrderBook -> Features -> Strategy
```

Planned shape:

```text
OrderBook -> Features -> StatisticalState -> Strategy -> Risk
```

The statistical layer should compute rolling context such as:

- Z-score of microprice edge
- Z-score of OFI
- Z-score of top-level imbalance
- Z-score of top-N imbalance
- Rolling standard deviation of mid-price returns
- Rolling linear-regression slope of mid price or microprice
- Expected move in ticks
- Enough-samples flags for warmup safety

The strategy layer can then ask:

```text
Is there directional pressure?
Is the signal statistically abnormal?
Is the expected move larger than spread + fees + slippage buffer?
Is volatility low enough that the stop-loss is sane?
```

For example:

```text
BUY only if:
  microstructure is bullish
  microprice edge z-score is high
  imbalance z-score confirms the direction
  expected move in ticks is greater than spread + fees + safety margin
```

Risk should eventually account for:

- Entry side: buy crosses at ask, sell crosses at bid
- Spread
- Fees
- Expected slippage
- Rolling volatility
- Take-profit distance
- Stop-loss distance
- Maximum position size
- Maximum loss per trade
- Cooldown after adverse moves

Polynomial regression may be useful as an offline research tool, but it should
not be the first C++ hot-path implementation. The first production-like
statistical filter should be cheap, stable, and interpretable:

- Rolling z-scores
- Rolling variance/stddev
- Rolling linear slope
- Spread-adjusted expected edge

More complex models can be evaluated in Python first and ported only if they
add real predictive value after spread and fees.

## Low-Latency Concepts Demonstrated

This repository is intentionally focused on fundamentals that matter in
latency-sensitive C++ systems:

- Bounded queues instead of unbounded shared containers
- Single-writer/single-reader ownership between pipeline stages
- Atomic memory ordering instead of mutex-based handoff
- Cache-line awareness around frequently written indexes
- Fixed-size arrays for predictable order book storage
- Packed bitmaps for compact active-level tracking
- Bit operations for best-price and top-N discovery
- Keeping calculations out of the order book state object
- Separating hot-path processing from telemetry/export work
- Carrying metadata through the full pipeline
- Measuring each stage separately instead of relying on one end-to-end number
- Using telemetry to make performance work visible

## Tech Stack

- C++23
- CMake
- Conan
- Boost.Asio
- Boost.Beast
- OpenSSL
- simdjson
- AMQP-CPP
- RabbitMQ
- `std::jthread`
- Custom SPSC ring buffer
- Python/TypeScript dashboard planned for telemetry visualization

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

## Known Risks And Open Issues

This section is intentionally explicit because correctness matters more than
looking finished.

- The system currently uses Kraken public WebSocket data, so network latency is
  dominated by public Internet routing and exchange-side timing.
- Exchange-to-local latency depends on wall-clock synchronization.
- Local stage latency should use `steady_clock`, not wall-clock time.
- Debug `std::cout` calls in the hot path distort latency.
- Kraken checksum validation is not complete yet.
- Gap detection and reconnect recovery are not complete yet.
- Depth truncation requires careful handling. If an update falls outside the
  maintained top-N/depth window, feature calculation may need to skip, resync,
  or mark the state stale.
- Fixed-size price arrays need bounds checks or resync behavior if the market
  moves outside the supported index range.
- The feature layer currently depends on previous top-N snapshots for OFI, so
  snapshot update timing must be handled carefully.
- The strategy is currently an interpretable rule engine, not a validated
  profitable trading strategy.
- Risk, execution, order management, and PnL accounting are not implemented.
- TLS peer verification is currently not production-hardened.

## Project Goals

The long-term goal is to build a small but serious trading infrastructure system
that demonstrates:

- Low-latency C++ design
- Market data feed handling
- Order book construction
- Event-driven architecture
- Concurrency and memory-layout awareness
- Microstructure feature engineering
- Strategy signal generation
- Statistical filtering
- Risk-aware trade gating
- Testing, profiling, and benchmarking discipline
- Observability through external dashboards

Planned improvements:

- Implement Kraken checksum validation for book correctness
- Add sequence/gap detection and reconnect logic
- Add robust snapshot/resync handling
- Add replayable market data tests
- Add microbenchmarks for parsing, queue handoff, order book updates, top-N
  lookup, feature calculation, and strategy decisions
- Publish structured JSON or binary telemetry over RabbitMQ
- Build a Python/TypeScript dashboard for live visualization
- Add rolling statistical features and z-score filters
- Add spread-aware take-profit and stop-loss calculations
- Add a risk module before any execution path
- Add a replay/backtest harness to validate whether signals predict future
  returns after spread and fees

## Notes

The project currently disables TLS peer verification in the WebSocket client
while the infrastructure is being built out. That should be changed before
treating the feed client as production-ready.

Generated build directories, IDE files, and compiled binaries should not be
committed.
