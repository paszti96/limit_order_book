# High-Performance Limit Order Book (Matching Engine) — Requirements

## 1. Purpose

Build a production-style limit order book + matching engine in modern C++ (C++17/20) that demonstrates:

- Correct **price–time priority** matching
- Strong engineering discipline: determinism, memory ownership, testability
- Measurable low-latency performance targeting **< 2 microseconds** for the **engine-only** hot path

This project is explicitly latency-oriented: a “correct but allocation-heavy” implementation is considered a failure.

---

## 2. Definitions

### 2.1 Latency definition (must be explicit)

**Engine-only latency** is measured from:

- **t0**: immediately after an incoming message is decoded into an internal `Command` POD  
to
- **t1**: immediately after the engine finishes processing the command and has emitted all resulting `Event`s into the output sink (e.g., ring buffer)

This metric **excludes**:
- syscalls, TCP read/write, kernel scheduling
- serialization / network framing (unless explicitly measured in separate benchmarks)

A second metric, **wire-to-ack**, may be reported separately.

### 2.2 Units and types

- `Price`: integer **ticks** (no floats)
- `Qty`: integer **lots**
- `OrderId`: `uint64_t`
- `Side`: `Buy` or `Sell`

---

## 3. Scope and Phases

### Phase 1 (MVP): Correct matching + basic performance
- Single instrument
- Limit orders + cancel by order id
- Price–time priority (FIFO per price level)
- Deterministic replay (golden output)
- Benchmark harness for engine-only latency

### Phase 2 (Optimization): Data-structure replacements
- Replace `std::map` with flatter structures (e.g., `flat_map` / sorted vector / ladder)
- Replace `unordered_map` if needed (custom fixed-capacity open addressing)
- Prove **no allocations in hot path**
- Document performance changes and trade-offs

### Phase 3 (Networking): TCP gateway
- Simple TCP server (optional)
- Fixed-size binary protocol (no JSON/text)
- Split threads optional: gateway thread(s) + engine thread + tx thread
- Separate “wire-to-ack” measurements

---

## 4. Functional Requirements

### 4.1 Supported commands

The engine must support:

1. **AddLimit**
   - Fields: `order_id`, `side`, `price`, `qty`
2. **Cancel**
   - Fields: `order_id`
3. **Modify/Replace** (optional Phase 1; required Phase 2)
   - Either:
     - `Modify(order_id, new_price?, new_qty?)`, or
     - `Replace(old_order_id, new_order_id, price, qty)` (cancel+add semantics)

### 4.2 Matching rules

- **Price–time priority (FIFO)** at each price level
- An incoming order is **aggressive** if it crosses the best price on the opposite side:
  - Buy crosses if `buy.price >= best_ask.price`
  - Sell crosses if `sell.price <= best_bid.price`

When aggressive:
- Match against resting liquidity starting at best price
- Generate one or more fills until:
  - incoming qty becomes 0 (fully filled), or
  - the book has no crossing liquidity

If incoming qty remains > 0:
- Rest remaining qty on the book at its limit price (FIFO at that level)

### 4.3 Validation and rejection

The engine must reject invalid commands (emit `REJECT` with a reason code):

- `qty <= 0`
- `price <= 0` for `AddLimit`
- invalid `side`
- duplicate `order_id` on `AddLimit`
- unknown `order_id` on `Cancel` (and Modify if supported)
- numeric overflow risk (policy must be explicit; recommended: reject)

### 4.4 Outputs (events)

For each processed command, the engine emits zero or more fixed-size POD `Event`s:

- `ACK` — accepted command (e.g., accepted add/cancel)
- `REJECT` — invalid command + reason code
- `FILL` — trade execution (includes maker/taker ids, price, qty)
- `CANCELLED` — cancellation completed
- `BOOK_UPDATE` — optional (Phase 2+; can be derived from stream)

**Event payloads must be fixed-size POD** (no dynamic allocation).

---

## 5. Non-Functional Requirements

### 5.1 Performance targets

**Engine-only hot path** targets (release/perf mode; pinned core recommended):

- Median latency: **< 2 µs**
- p99 latency: project-defined budget (recommended starting goal: **< 10 µs**)

Benchmarks must report:
- median / p95 / p99 latencies
- throughput (msgs/sec)
- CPU model, compiler version, build flags, pinning settings
- a precise definition of what is included/excluded in the measurement window

### 5.2 Hot-path memory rules

In the hot path (`Engine::process` → matching → event emission):

- **No `new` / `delete`**
- No allocations from STL containers (all reserved upfront; fixed-size where possible)
- No `std::string`, iostream, logging, exceptions
- Prefer `noexcept` where practical

All memory for:
- orders
- per-order links (intrusive)
- command/event queues
must be preallocated at startup.

### 5.3 Determinism

- The matching engine must be a **single-writer** for book state.
- Given the same command stream, the resulting event stream must be identical.

### 5.4 Reliability / safety

- Overflow/underflow rules must be explicit.
- Debug builds:
  - assertions enabled
  - sanitizers optionally enabled

### 5.5 Observability (non-hot path)

Counters (thread-local or atomics + aggregation):
- processed commands
- rejects
- trades
- cancels
- live orders
- optional: best bid/ask, depth stats

Optional trace sampling (disabled by default in perf builds).

---

## 6. Acceptance Criteria

### 6.1 Correctness
- Unit tests cover:
  - FIFO at same price
  - partial fills
  - multi-level sweeps
  - cancel head/middle/tail
  - rejection cases
- Deterministic replay test:
  - input command file produces a stable golden output file

### 6.2 Performance
- Benchmark harness included under `bench/`
- Results + reproduction steps documented under `docs/performance.md`
- Hot-path allocation-free verified via one or more:
  - overridden global `operator new` in perf mode (abort on alloc)
  - malloc tracing (e.g., `LD_PRELOAD`) in tests
  - `perf` / heap-tracking evidence

---

## 7. Out of Scope (initially)

- Auctions / opening cross
- Market orders, IOC/FOK, hidden/iceberg, pegged orders
- STP (self-trade prevention)
- Risk limits / credit checks
- Persistence / recovery
- Multi-instrument routing
- Full market data dissemination
