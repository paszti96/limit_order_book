
Notes:
- Engine thread is the **single writer** of book state.
- Gateway threads perform I/O and encoding/decoding only.
- Rings are preallocated fixed-size buffers.

---

## 3. Component Breakdown

### 3.1 `common/`
**Responsibilities**
- Define core POD types and enums:
  - `Price`, `Qty`, `OrderId`, `Side`
  - `Command`, `Event`
- Utility helpers for time measurement (optional):
  - `rdtsc` wrappers / `clock_gettime` wrappers (bench only)

**Rules**
- No heap allocation.
- Keep types trivially copyable where possible.

---

### 3.2 `pool/`
**Responsibilities**
- `ObjectPool<Order>`: fixed-capacity pool for orders
  - preallocated storage (e.g., `std::vector<Order>`)
  - free-list for recycling
- `OrderIndex`: map `OrderId -> handle/pointer`
  - Phase 1: `std::unordered_map` with `reserve()` and possibly custom allocator
  - Phase 2: custom open-addressing hash table (fixed capacity)

**Why it exists**
- Ensures constant-time order lookup for cancel/modify.
- Ensures hot path has no `new/delete`.

---

### 3.3 `book/`
Contains the order book state and book-side structures.

#### `PriceLevel`
**Responsibilities**
- Represents a single price level:
  - FIFO queue of orders (time priority)
  - aggregate quantity at that level

**Recommended implementation**
- Intrusive linked list:
  - each `Order` contains `prev/next` links (indices or pointers)
  - O(1) remove for cancellations

#### `OrderBookSide`
**Responsibilities**
- Owns and manages price levels for one side (bids OR asks).
- Provides:
  - find best price
  - insert/find a price level
  - remove empty levels

**Phase 1 structure**
- `std::map<Price, PriceLevel>`:
  - bids sorted descending
  - asks sorted ascending

**Phase 2 structure options**
- `boost::container::flat_map`
- sorted `std::vector<PriceLevel>` + binary search
- bounded tick ladder (if tick range is known)

#### `OrderBook`
**Responsibilities**
- Holds both sides:
  - `bids`, `asks`
- Exposes operations used by engine:
  - `add_limit(order)`
  - `cancel(order_id)`
  - `match(order)` (internal)

**Invariant**
- Book state is modified only by the engine thread.

---

### 3.4 `engine/`
#### `MatchingEngine`
**Responsibilities**
- Single entry point:
  - `process(const Command&, EventSink&) noexcept`
- Performs:
  - validation
  - matching
  - resting orders
  - cancel/modify
  - event emission

**Hot path constraints**
- No syscalls
- No allocations
- No exceptions / logging

#### `RiskChecks` (optional)
- Cheap checks only (e.g., max qty)
- Must obey hot path constraints

---

### 3.5 `io/` (Phase 3)
#### `codec/`
- Encode/decode `Command` and `Event` from/to a fixed binary format.
- Avoid variable-length fields.

#### `tcp/`
- TCP accept loop + per-session I/O handling.
- Recommended:
  - `RX thread`: read buffer → decode → command ring
  - `TX thread`: event ring → encode → write

---

### 3.6 `bench/`
**Responsibilities**
- Synthetic workload generation:
  - mix of adds/cancels/aggressive orders
  - configurable depth, level count, order distributions
- Latency measurement:
  - record per-command engine-only timings
  - histogram / percentiles
- Throughput measurement

**Rules**
- Keep measurement overhead minimal.
- Prefer batched stats aggregation to avoid perturbing latency.

---

### 3.7 `tests/`
**Responsibilities**
- Unit tests for matching semantics.
- Deterministic replay tests:
  - fixed input → fixed output (golden file)

---

## 4. Key Data Structures (Conceptual)

### 4.1 Order lifetime
1. `AddLimit`:
   - allocate from `ObjectPool`
   - insert into `OrderIndex`
   - link into `PriceLevel` FIFO
2. `Match`:
   - decrement `qty_remaining`
   - if 0: unlink, remove from index, return to pool
3. `Cancel`:
   - lookup via index
   - unlink in O(1)
   - remove from index, return to pool

### 4.2 Event emission
- Engine emits `Event` objects to an `EventSink` abstraction:
  - in tests: `std::vector<Event>` (reserved)
  - in perf mode: fixed-size ring buffer writer

---

## 5. Threading Model

### Phase 1/2
- Single-threaded engine + local sinks (simplest, deterministic, easiest to benchmark)

### Phase 3 (recommended)
- Engine thread is the **single writer** of book state.
- Gateway threads handle:
  - socket I/O
  - encoding/decoding
- Communication via preallocated rings:
  - `SPSC` rings are preferred when topology allows
  - `MPSC` only if multiple RX threads are used (optional)

---

## 6. Build Modes

- **Debug**
  - assertions enabled
  - sanitizers optional
- **Release**
  - optimized build for normal usage
- **Perf**
  - forbids allocations (optional: override global `operator new` to abort)
  - disables logging and heavy instrumentation

---

## 7. Extensibility (Planned)

- Multi-instrument:
  - `InstrumentId -> OrderBook` mapping
  - still enforce single-writer per book (or sharded engines)
- More order types:
  - IOC/FOK
  - Market order (implemented as aggressive limit with sentinel price)
- Market data snapshots:
  - reconstruct from events
  - or maintain incremental top-of-book in a separate component
