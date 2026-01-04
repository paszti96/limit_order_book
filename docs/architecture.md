# High-Performance Limit Order Book (Matching Engine) — Architecture

## 1. Overview

This system is a deterministic, allocation-free (hot path) matching engine designed as a pipeline:

**Command (decoded input) → Engine (single-writer match) → Events (fixed-size outputs)**

Primary design constraints:
- **Single writer** mutates order book state (determinism).
- **No syscalls** in the matching core.
- **No dynamic allocation** in the hot path after startup.
- **Binary POD protocol** at boundaries (no JSON/text).

---

## 2. Architecture Diagram

# Phase 1 / Phase 2 - No networking


+-------------------------+
| Workload / Tests / CLI  |
| (generates Command POD) |
+-----------+-------------+
            |
            v
+-------------------------+
|     MatchingEngine      |
|  - validate             |
|  - match (price-time)   |
|  - rest / cancel        |
|  - emit Events          |
|  (single writer)        |
+-----------+-------------+
            |
            v
+-------------------------+
|       EventSink         |
|  - vector (tests)       |
|  - ring buffer (perf)   |
+-------------------------+

# Phase 3 - TCP Mode

+-----------+     +------------------+     +------------------+
|  Socket   | --> | RX / Gateway     | --> |  Command Ring    |
|           |     | read + decode    |     | (preallocated)   |
+-----------+     +------------------+     +--------+---------+
                                                       |
                                                       v
                                            +------------------+
                                            | Engine Thread    |
                                            | MatchingEngine   |
                                            | (single writer)  |
                                            +--------+---------+
                                                       |
                                                       v
+-----------+     +------------------+     +------------------+
|  Socket   | <-- | TX Thread        | <-- |   Event Ring     |
|           |     | encode + write   |     | (preallocated)   |
+-----------+     +------------------+     +------------------+


# Final diagram: 

┌─────────────────────────────────────────────────────┐
│                  TCP Server (Phase 3)               │
│  ┌─────────────────────────────────────────────┐    │
│  │   Parse incoming orders (simple text format)│    │
│  └──────────────────┬──────────────────────────┘    │
└────────────────────│────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│              Matching Engine (Core)                 │
│  ┌──────────────────────────────────────────────┐   │
│  │  Order Validation & Routing                  │   │
│  │  - Check price/qty validity                  │   │
│  │  - Route to Bid/Ask book                     │   │
│  └──────────────┬───────────────────────────────┘   │
│                 │                                   │
│  ┌──────────────▼──────────┐  ┌──────────────────┐  │
│  │   Bid Book (std::map)   │  │  Ask Book        │  │
│  │   Key: Price (desc)     │  │  Key: Price (asc)│  │
│  │   Value: Queue<Order>   │  │  Value: Queue    │  │
│  └──────────────┬──────────┘  └────────┬─────────┘  │
│                 │                      │            │
│  ┌──────────────▼───────────────────────▼─────────┐ │
│  │         Matching Algorithm                     │ │
│  │  - Walk book until fill or no match            │ │
│  │  - Generate Trade objects                      │ │
│  │  - Update/remove resting orders                │ │
│  └────────────────────────────────────────────────┘ │
└─────────────────────────────────────────────────────┘
                     │
                     ▼
┌─────────────────────────────────────────────────────┐
│              Object Pools (Memory Mgmt)             │
│  - Order Pool: std::vector<Order> (preallocated)    │
│  - Trade Pool: Reuse trade objects                  │
│  - NO malloc/free in hot path                       │
└─────────────────────────────────────────────────────┘

Notes:
- Prefer **SPSC** rings when possible (RX→Engine, Engine→TX).
- Use MPSC only if multiple RX threads are introduced (optional extension).

---

## 3. Key Invariants

### 3.1 Determinism
- Engine processes a totally ordered stream of `Command`s.
- Given identical input streams, emitted `Event`s are identical.
- Matching core is single-threaded (or logically single-writer).

### 3.2 Hot-path constraints
Inside `MatchingEngine::process()` and any code it calls:
- No `new/delete`, no container growth/rehash
- No `std::string`, iostreams, logging
- No blocking calls, no syscalls
- Prefer `noexcept` for core processing surface

### 3.3 Ownership and lifetime
- Resting orders live in an **OrderPool** (fixed capacity).
- Cancel/fill recycles orders back to the pool.
- Order lookup is via an **OrderIndex**: `OrderId -> handle`.

---

## 4. Data Model

### 4.1 Core types (in `common/`)
- `using OrderId = uint64_t;`
- `using Price   = int64_t;`  (ticks)
- `using Qty     = int32_t;`  (lots)
- `enum class Side : uint8_t { Buy, Sell };`

### 4.2 Commands and Events (POD boundaries)

#### `Command` (input to engine)
- Fixed-size POD designed to be binary-protocol friendly.
- Must contain:
  - type: AddLimit / Cancel / Modify (optional)
  - order_id (and possibly new_order_id for Replace)
  - side (for AddLimit)
  - price, qty (for AddLimit / Replace / Modify as defined)

Illustrative example (exact layout may differ):
```cpp
enum class CmdType : uint8_t { AddLimit, Cancel, Modify };

struct Command {
  CmdType   type;
  Side      side;       // valid for AddLimit
  uint16_t  _pad0;
  uint32_t  qty;        // 0 invalid
  int64_t   price;      // 0 invalid for AddLimit
  uint64_t  order_id;
  uint64_t  order_id2;  // optional: used for Replace semantics
};
