# VortexKV

A high-performance, multithreaded, shared-nothing, Redis-compatible in-memory key-value store written in modern C++20. Evolved from the [single-threaded RapidKV](https://github.com/shashank2602/RapidKV) that was built to outperform Redis, VortexKV scales across all available cores to challenge [Dragonfly](https://github.com/dragonflydb/dragonfly) — the fastest multithreaded Redis alternative.

> **~11.9M SET ops/sec pipelined · ~9.9M GET ops/sec pipelined · Up to 3.7× faster than Dragonfly on pipelined reads · Sub-millisecond p99 latency across the board**

---

## Highlights

- **11.9M ops/sec** peak pipelined SET throughput (1.6× Dragonfly)
- **9.9M ops/sec** peak pipelined GET throughput (3.7× Dragonfly)
- **2.58M ops/sec** non-pipelined SET/GET — matching Dragonfly head-to-head
- **Shared-nothing architecture** — each shard owns its thread, io_context, database, and allocator; zero cross-shard locking
- **Near-linear shard scaling** with hash-based key routing and `asio::post` for cross-shard requests
- All the single-threaded RapidKV innovations retained: Robin Hood FlatMap (SoA), CompactString, zero-copy RESP parser, slab allocator, SIEVE eviction

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                              CLIENT CONNECTIONS (TCP)                               │
│                          memtier / redis-cli / any RESP client                      │
└──────────────────────────────────┬──────────────────────────────────────────────────┘
                                   │ TCP accept()
                                   ▼
┌─────────────────────────────────────────────────────────────────────────────────────┐
│                        SERVER (main thread, single acceptor)                        │
│  ┌───────────────────────────────────────────────────────────────────────────────┐  │
│  │  • asio::io_context for accept loop only                                      │  │
│  │  • Round-robin connection assignment → GetNextShard()                         │  │
│  │  • Generates global routing hash seed (randomized)                            │  │
│  │  • Creates N Shard objects (N = hardware_concurrency or config)               │  │
│  └───────────────────────────────────────────────────────────────────────────────┘  │
└──────────┬──────────┬──────────┬──────────────────────┬──────────┬──────────────────┘
           │          │          │          ...         │          │
    socket move  socket move  socket move          socket move  socket move
           │          │          │                      │          │
           ▼          ▼          ▼                      ▼          ▼
┌──────────────┐┌──────────────┐┌──────────────┐┌──────────────┐┌──────────────┐
│   SHARD 0    ││   SHARD 1    ││   SHARD 2    ││    ...       ││  SHARD N-1   │
│ (pinned to   ││ (pinned to   ││ (pinned to   ││              ││ (pinned to   │
│  core 0)     ││  core 1)     ││  core 2)     ││              ││  core N-1)   │
│──────────────││──────────────││──────────────││──────────────││──────────────│
│ Own thread   ││ Own thread   ││ Own thread   ││ Own thread   ││ Own thread   │
│(std::jthread)││(std::jthread)││(std::jthread)││(std::jthread)││(std::jthread)│
│──────────────││──────────────││──────────────││──────────────││──────────────│
│Own io_context││Own io_context││Own io_context││Own io_context││Own io_context│
│──────────────││──────────────││──────────────││──────────────││──────────────│
│Own Database  ││Own Database  ││Own Database  ││Own Database  ││Own Database  │
│──────────────││──────────────││──────────────││──────────────││──────────────│
│Own Dispatcher││Own Dispatcher││Own Dispatcher││Own Dispatcher││Own Dispatcher│
│──────────────││──────────────││──────────────││──────────────││──────────────│
│Maintenance   ││Maintenance   ││Maintenance   ││Maintenance   ││Maintenance   │
│Timer (TTL)   ││Timer (TTL)   ││Timer (TTL)   ││Timer (TTL)   ││Timer (TTL)   │
└──────┬───────┘└──────────────┘└──────────────┘└──────────────┘└──────────────┘
       │
       │  Each shard manages multiple Connection objects
       ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                         CONNECTION (per-client)                                 │
│  ┌───────────────────────────────────────────────────────────────────────────┐  │
│  │  LinearBuffer (read)        │  Double-Buffered Write (primary/secondary)  │  │
│  │  with compaction            │  swap-and-send on async_write completion    │  │
│  ├───────────────────────────────────────────────────────────────────────────┤  │
│  │  Zero-Copy RESP Parser      │  Zero-Alloc RESP Writer                     │  │
│  │  (string_view into buffer)  │  (stack buffers + reserveContiguous)        │  │
│  ├───────────────────────────────────────────────────────────────────────────┤  │
│  │        Pipeline Batch (parse all commands in one read)                    │  │
│  ├───────────────────────────────────────────────────────────────────────────┤  │
│  │   KEY-BASED ROUTING: hash(key, seed) % N_shards                           │  │
│  │   ┌─────────────────────┐   ┌──────────────────────────┐                  │  │
│  │   │ Local: same shard   │   │ Remote: asio::post to    │                  │  │
│  │   │ → direct dispatch() │   │ target shard io_context  │                  │  │
│  │   │                     │   │ → callback posts back    │                  │  │
│  │   └─────────────────────┘   └──────────────────────────┘                  │  │
│  ├───────────────────────────────────────────────────────────────────────────┤  │
│  │  Command Dispatcher (uint64_t-packed name → O(1) linear scan in L1)       │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
└─────────────────────────────────────┬───────────────────────────────────────────┘
                                      │
                                      ▼
┌─────────────────────────────────────────────────────────────────────────────────┐
│                          DATABASE LAYER (per-shard)                             │
│  ┌───────────────────────────────────────────────────────────────────────────┐  │
│  │           Robin Hood FlatMap (Structure-of-Arrays layout)                 │  │
│  │  ┌────────────┬────────────┬────────────────┬─────────────┐               │  │
│  │  │ metadata[] │  keys[]    │   values[]     │  hashes[]   │               │  │
│  │  │ (2B/slot)  │(CompactStr)│(variant<CS,ll>)│ (uint32_t)  │               │  │
│  │  └────────────┴────────────┴────────────────┴─────────────┘               │  │
│  │  • 7-bit hash tag early rejection (~99% false-positive avoidance)         │  │
│  │  • Incremental resizing (no latency spikes)                               │  │
│  │  • SIEVE eviction (visited bit in metadata byte)                          │  │                   │  │
│  └───────────────────────────────────────────────────────────────────────────┘  │
│  ┌────────────────────────────┐  ┌────────────────────────────────────────┐     │
│  │  CompactString (16B SSO)   │  │  Slab Allocator (thread_local)         │     │
│  │  • Inline ≤15 bytes        │  │  • 7 size classes: 32B–2048B           │     │
│  │  • 16-byte aligned         │  │  • 1MB page allocation                 │     │
│  │  • Binary-safe, move-only  │  │  • O(1) alloc/dealloc free-list        │     │
│  │  • 4 keys per cache line   │  │  • Fallback to ::operator new          │     │
│  └────────────────────────────┘  └────────────────────────────────────────┘     │
└─────────────────────────────────────────────────────────────────────────────────┘

EXTERNAL DEPENDENCIES:
┌──────────────┬──────────────┬──────────────┬──────────────┐
│ Asio 1.36    │ mimalloc 2.2 │ rapidhash    │ libdivide    │
│ (async I/O)  │ (allocator   │ (fast hash   │ (fast modulo │
│              │  override)   │  function)   │  for shard   │
│              │              │              │  routing)    │
└──────────────┴──────────────┴──────────────┴──────────────┘
```
>With shard = 1, VortexKV matches the single-threaded RapidKV within a few percent — validating that the shared-nothing abstraction adds near-zero overhead when cross-shard routing isn't needed.

---

## Key Design Decisions

### Shared-Nothing Multithreading
- Each **Shard** owns its own `asio::io_context`, `std::jthread`, `Database`, and `CommandDispatcher` — no shared mutable state, no mutexes, no atomic contention on the hot path
- Shards are **cache-line aligned** (`alignas(64)`) to prevent false sharing
- Each shard thread is **pinned to a dedicated CPU core** via `PinThreadToCore()` for optimal cache locality
- The main thread's acceptor distributes new connections **round-robin** across shards
- Memory budget is **divided equally** across shards (`maxMemoryUsage / shardCount`)

### Hash-Based Key Routing
- Every command is routed to the shard that owns its key: `rapidhash(key, seed) % shard_count`
- **libdivide** provides branchless fast modulus — avoids expensive hardware division on every request
- If a command targets the **local shard**, it executes inline with zero overhead
- If it targets a **remote shard**, the request is posted to that shard's `io_context` via `ExecuteRemote()`, which executes the command and posts the response back to the caller's context — fully asynchronous, no blocking

### Per-Connection Software Pipeline
- All RESP commands in a single read are parsed into a batch (`m_pipelinedRequests`)
- Each command is routed independently; local commands complete immediately, remote commands complete asynchronously
- A **completion counter** tracks outstanding responses; `FlushPipelinedResponses()` fires only when all commands in the batch have completed, preserving response ordering

### Async TCP Server with Asio
- Each shard runs its own event loop (`io_context::run()`) — all I/O and maintenance timers run on the shard's thread
- Connections use **async reads/writes** with `std::shared_from_this()` lifetime management
- `TCP_NODELAY` enabled on every accepted socket
- `asio::recycling_allocator` used for handler allocation to avoid heap churn
- Socket handoff from acceptor thread to shard uses `release()`/`assign()` to transfer the native FD without cross-thread `io_context` conflicts

### CompactString — 16-Byte SSO String
- Union-based string that stores strings ≤15 bytes inline without heap allocation
- Heap-allocated strings use the slab allocator to prevent fragmentation
- Aligned to 16 bytes — four keys fit in a single 64-byte cache line during hash table probing

### Robin Hood FlatMap — Structure of Arrays
- **SoA layout**: Separate arrays for keys, values, metadata, and hashes — metadata scanning touches only 2-byte entries for excellent cache utilization
- **2-byte metadata per slot**: 1 byte PSL + 1 byte tag (7 hash bits + 1 visited bit)
- **Tag-based early rejection**: Check PSL + 7-bit hash tag before comparing keys — avoids ~99% of false-positive key comparisons
- **Incremental resizing**: Entries migrated N-at-a-time per operation, spreading resize cost across requests — no latency spikes
- **SIEVE eviction**: Clock-hand algorithm using the visited bit — approximates LRU with O(1) bookkeeping

### Command Dispatcher — Integer-Packed Lookup
- Command names (≤8 chars) are uppercased and packed into a `uint64_t` — dispatch is a single integer comparison instead of `strcmp`
- Fixed-size array of 32 `CommandEntry` structs fits in L1 cache; hottest commands (GET, SET) registered first for early exit

### Zero-Copy RESP Parser & Zero-Alloc Writer
- Parser produces `string_view` directly into the network read buffer — no allocations, no copies
- Writer uses `std::to_chars` into stack buffers and writes directly into the `LinearBuffer`

### Double-Buffered Async Write
- Two response buffers per connection (primary + secondary)
- While the OS writes the primary buffer, new responses accumulate in the secondary
- Swap-and-send on completion — no write stalls

### Slab Allocator
- 7 size classes (32B–2048B) with free-list allocation
- 1MB page allocation to reduce `mmap` syscalls
- O(1) alloc/dealloc; fallback to `::operator new` for oversized allocations

### mimalloc Global Allocator
- Microsoft's [mimalloc](https://github.com/microsoft/mimalloc) replaces the default allocator via `MI_OVERRIDE`, providing better multithreaded allocation performance and reduced fragmentation

---

## Supported Commands

| Command | Description |
|---------|-------------|
| `PING [message]` | Connection test |
| `ECHO message` | Echo back message |
| `SET key value [PX milliseconds]` | Set key-value with optional TTL |
| `GET key` | Retrieve value |
| `DEL key` | Delete a key |
| `EXISTS key` | Check key existence |
| `INCR key` | Increment integer value |
| `DECR key` | Decrement integer value |
| `INCRBY key delta` | Increment by delta |
| `DECRBY key delta` | Decrement by delta |
| `EXPIRE key milliseconds` | Set TTL on key |
| `PERSIST key` | Remove TTL |
| `TTL key` | Get remaining TTL |

> **Note:** `DEL` and `EXISTS` currently operate on a single key only. Multi-key variants (`DEL key1 key2 ...`, `EXISTS key1 key2 ...`) are not supported due to the shared-nothing architecture — each key may reside on a different shard, requiring cross-shard coordination for atomically counting results across multiple keys.

---

## Benchmarks

### Test Environment

| Component | Details |
|---|---|
| **CPU** | AMD EPYC 9554P (64C/128T) |
| **Server Cores** | 64 threads pinned to cores 0–63 |
| **Benchmark Cores** | 64 threads pinned to cores 64–127 via `taskset -c 64-127` |
| **Benchmark Tool** | `memtier_benchmark` |
| **Value Size** | 256 bytes |
| **Key Range** | 1–10,000,000 |

### Server Configurations

| Server | Launch Command                                                                                               |
|---|--------------------------------------------------------------------------------------------------------------|
| **Redis** | `redis-server --save "" --appendonly no`                                                                     |
| **Dragonfly** | `dragonfly --dbfilename "" --snapshot_cron "" --cache_mode=true --version_check=false --proactor_threads=64` |
| **VortexKV** | `./VortexKV VortexKV.config` (64 shards)                                                                       |

> All three servers ran on the same bare-metal machine. Redis was configured with all persistence disabled. Dragonfly was configured in cache mode with snapshots disabled. Servers were restarted between runs for a clean state.

---

### 1. SET Throughput (No Pipeline)

> **64 threads × 10 connections, ratio 1:0 (SET only), 256B values, 100 seconds**

| Metric | Redis | Dragonfly | VortexKV | VortexKV vs Redis | VortexKV vs Dragonfly |
|---|---|---|---|---|---|
| **Ops/sec** | 66,631 | 2,489,825 | **2,575,169** | **38.6×** | **+3.4%** |
| **Avg Latency** | 9.508 ms | 0.254 ms | **0.248 ms** | **−97.4%** | **−2.4%** |
| **p99 Latency** | 11.967 ms | 0.351 ms | **0.343 ms** | **−97.1%** | **−2.3%** |
| **p99.9 Latency** | 19.967 ms | 0.607 ms | **0.695 ms** | **−96.5%** | +14.5% |

---

### 2. GET Throughput (No Pipeline, Pre-populated)

> **64 threads × 10 connections, ratio 0:1 (GET only), 256B values, 100 seconds, 10M keys pre-populated**

| Metric | Redis | Dragonfly | VortexKV | VortexKV vs Redis | VortexKV vs Dragonfly |
|---|---|---|---|---|---|
| **Ops/sec** | 70,780 | 2,552,889 | **2,541,428** | **35.9×** | −0.4% |
| **Avg Latency** | 9.131 ms | 0.253 ms | **0.249 ms** | **−97.3%** | **−1.6%** |
| **p99 Latency** | 9.407 ms | 0.351 ms | **0.343 ms** | **−96.4%** | **−2.3%** |
| **p99.9 Latency** | 18.303 ms | 0.535 ms | **0.511 ms** | **−97.2%** | **−4.5%** |

**Key takeaway:** Without pipelining, VortexKV and Dragonfly are effectively neck-and-neck at ~2.5M ops/sec — both ~36× faster than single-threaded Redis. VortexKV has a slight latency edge.

---

### 3. Pipelined SET (Pipeline = 30)

> **64 threads × 10 connections, pipeline = 30, ratio 1:0, 256B values, 200K requests/client**

| Metric | Redis (16t×10c) | Dragonfly | VortexKV | VortexKV vs Redis | VortexKV vs Dragonfly |
|---|---|---|---|---|---|
| **Ops/sec** | 567,642 | 7,417,396 | **11,880,297** | **20.9×** | **+60.2%** |
| **Avg Latency** | 8.298 ms | 2.358 ms | **1.769 ms** | **−78.7%** | **−25.0%** |
| **p99 Latency** | 15.743 ms | 4.047 ms | **2.319 ms** | **−85.3%** | **−42.7%** |
| **p99.9 Latency** | 23.679 ms | 5.663 ms | **5.695 ms** | **−75.9%** | +0.6% |

---

### 4. Pipelined GET (Pipeline = 30, Pre-populated)

> **64 threads × 10 connections, pipeline = 30, ratio 0:1, 256B values, 100 seconds, 10M keys pre-populated**

| Metric | Redis (16t×10c) | Dragonfly | VortexKV | VortexKV vs Redis | VortexKV vs Dragonfly |
|---|---|---|---|---|---|
| **Ops/sec** | 558,308 | 2,688,873 | **9,889,346** | **17.7×** | **3.68×** |
| **Avg Latency** | 8.592 ms | 7.140 ms | **1.918 ms** | **−77.7%** | **−73.1%** |
| **p99 Latency** | 17.023 ms | 7.807 ms | **2.655 ms** | **−84.4%** | **−66.0%** |
| **p99.9 Latency** | 17.279 ms | 8.767 ms | **3.023 ms** | **−82.5%** | **−65.5%** |

**Key takeaway:** Under pipelining, VortexKV's shared-nothing architecture with per-shard databases dominates. Pipelined SET is **60% faster** than Dragonfly; pipelined GET is **3.7× faster** — the biggest gap in the entire suite.

---

### 5. Shard Scaling (1:1 SET:GET, No Pipeline)

> **256B values, 25 seconds, scaling client threads proportionally to server shards**

#### Dragonfly

| Shards | Threads × Clients | Ops/sec | Avg Latency | p99 Latency | p99.9 Latency |
|---|---|---|---|---|---|
| 1 | 2 × 10 | 83,664 | 0.239 ms | 0.287 ms | 0.407 ms |
| 4 | 8 × 10 | 311,311 | 0.257 ms | 0.367 ms | 0.407 ms |
| 16 | 32 × 10 | 1,064,231 | 0.301 ms | 0.415 ms | 0.495 ms |
| 32 | 64 × 10 | 1,736,663 | 0.384 ms | 0.559 ms | 0.655 ms |
| 64 | 64 × 10 | 2,403,368 | 0.256 ms | 0.351 ms | 0.495 ms |

#### VortexKV

| Shards | Threads × Clients | Ops/sec | Avg Latency | p99 Latency | p99.9 Latency |
|---|---|---|---|---|---|
| 1 | 2 × 10 | 73,510 | 0.272 ms | 0.295 ms | 0.855 ms |
| 4 | 8 × 10 | 287,612 | 0.278 ms | 0.415 ms | 0.855 ms |
| 16 | 32 × 10 | 959,061 | 0.321 ms | 0.447 ms | 0.895 ms |
| 32 | 64 × 10 | 1,572,802 | 0.391 ms | 0.575 ms | 0.951 ms |
| 64 | 64 × 10 | 2,551,297 | 0.250 ms | 0.351 ms | 0.959 ms |

#### Head-to-Head Comparison

| Shards | Dragonfly (ops/sec) | VortexKV (ops/sec) | Δ | Dragonfly p99 | VortexKV p99 |
|---|---|---|---|---|---|
| 1 | 83,664 | 73,510 | −12.1% | 0.287 ms | 0.295 ms |
| 4 | 311,311 | 287,612 | −7.6% | 0.367 ms | 0.415 ms |
| 16 | 1,064,231 | 959,061 | −9.9% | 0.415 ms | 0.447 ms |
| 32 | 1,736,663 | 1,572,802 | −9.4% | 0.559 ms | 0.575 ms |
| 64 | 2,403,368 | 2,551,297 | **+6.2%** | 0.351 ms | 0.351 ms |

**Key takeaway:** At low shard counts, Dragonfly leads by ~8–12% — likely due to its mature per-thread optimizations. As shards scale up, the gap narrows significantly, and at full 64-core saturation RapidKV overtakes Dragonfly with **+6.2% higher throughput** and matching p99 latency. The shared-nothing architecture's advantage materializes at high core counts where cross-core contention becomes the bottleneck.

---

## Building

### Requirements

| Requirement | Minimum Version |
|---|---|
| **C++ Standard** | C++20 |
| **CMake** | 3.20+ |
| **Compiler** | GCC 12+ / Clang 15+ / MSVC 2022+ |
| **Git** | Any recent version (for FetchContent) |

### Dependencies (auto-fetched via CMake FetchContent)

| Dependency | Version | Purpose |
|---|---|---|
| [Asio](https://github.com/chriskohlhoff/asio) | 1.36.0 | Async networking (header-only) |
| [mimalloc](https://github.com/microsoft/mimalloc) | 2.2.7 | High-performance allocator |
| [RapidHash](https://github.com/nicbarker/rapidhash) | vendored | Fast hashing |
| [libdivide](https://github.com/ridiculousfish/libdivide) | vendored | Fast integer division |
| [Google Test](https://github.com/google/googletest) | 1.15.2 | Unit testing (optional) |
| [Google Benchmark](https://github.com/google/benchmark) | 1.9.1 | Micro-benchmarks (optional) |

### Linux

```bash
# Clone
git clone https://github.com/shashank2602/VortexKV.git
cd VortexKV

# Build (Release)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Build with tests and benchmarks
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXTRAS=ON
cmake --build . -j$(nproc)

# Run
./VortexKV                    # default: 0.0.0.0:8080
./VortexKV ../VortexKV.config   # custom config

# Run tests
ctest --output-on-failure

# Or run individual test binaries
./tests/TestCompactString
./tests/TestFlatMap
./tests/TestDatabase
./tests/TestRespProtocol
./tests/TestIntegration

# Run benchmarks
./benchmarks/BenchmarkFlatMap
./benchmarks/BenchmarkCompactString
./benchmarks/BenchmarkDatabase
./benchmarks/BenchmarkCommandDispatcher
```

### Windows (MSVC)

```powershell
# Clone
git clone https://github.com/shashank2602/VortexKV.git
cd VortexKV

# Build (Release)
mkdir build && cd build
cmake ..
cmake --build . --config Release

# Build with tests and benchmarks
cmake .. -DBUILD_EXTRAS=ON
cmake --build . --config Release

# Run
.\Release\VortexKV.exe
.\Release\VortexKV.exe ..\VortexKV.config

# Run tests
ctest -C Release --output-on-failure
```

---

## Configuration

```
# VortexKV.config
port 8080
bind 0.0.0.0
shards 64                    # defaults to hardware_concurrency
maintenance_interval_ms 200
max_memory_usage 4294967296  # 4 GB
```

---

## Project Structure

```
VortexKV/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── server/
│   │   ├── server.cpp              # Acceptor, shard pool management
│   │   ├── shard.cpp               # Per-shard thread, io_context, maintenance
│   │   └── connection.cpp          # Per-connection pipeline, routing, I/O
│   ├── commands/
│   │   ├── commandDispatcher.cpp   # Integer-packed command dispatch
│   │   └── commands.cpp            # Command implementations
│   ├── protocol/
│   │   ├── respParser.cpp          # Zero-copy RESP parser
│   │   └── respWriter.cpp          # Zero-alloc RESP writer
│   ├── storage/
│   │   ├── database.cpp            # Database operations (SET, GET, DEL, etc.)
│   │   ├── compactString.cpp       # 16-byte SSO string
│   │   └── slabAllocator.cpp       # Slab allocator (7 size classes)
│   └── utility/
│       ├── utility.cpp             # Helpers, thread pinning
│       └── linearBuffer.cpp        # Linear buffer implementation
├── inc/                            # Headers (mirrors src/ structure)
├── external/
│   ├── rapidhash/                  # rapidhash for key routing
│   └── libdivide/                  # Fast integer division
├── tests/                          # Google Test suite
├── benchmarks/                     # Google Benchmark microbenchmarks
├── VortexKV.config                 # Sample configuration
└── CMakeLists.txt
```

---

## Limitations

- **String-only values** — no lists, sets, sorted sets, or hashes
- **No persistence** — all data is in-memory only (no RDB/AOF snapshots)
- **No replication or clustering**
- **No AUTH, ACL, or TLS support**
- **Limited command set** — 13 commands
- **Single-key DEL/EXISTS** — multi-key variants not yet supported (see [Supported Commands](#supported-commands))
- **Little-endian only** — The CompactString SSO bit layout assumes little-endian architecture

---

## License

This project is open source.
