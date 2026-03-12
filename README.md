#  RapidKV

A high-performance, single-threaded, Redis-compatible in-memory key-value store written in modern C++20. Engineered from the ground up to beat Redis in raw throughput by leveraging cache-aware data structures, zero-copy parsing, and purpose-built memory management.

> **Up to ~2.6× faster than Redis on pipelined workloads, with significantly lower tail latency across the board even without pipelining.**

---

## Highlights


- **4.3M ops/sec** peak throughput with pipelining
- **1.52 GB/s** peak bandwidth under heavy load (2× Redis)
- **47–65% lower p99 latency** compared to Redis
- **2.5× fewer CPU instructions per command** than Redis (verified via `perf stat`)

---

## Architecture

```
┌──────────────────────────────────────────────────────┐
│                   Client Connections                 │
│         (Asio single-threaded event loop,            │
│          async TCP, TCP_NODELAY enabled)             │
├──────────────────────────────────────────────────────┤
│     LinearBuffer (read)    │   Double-Buffered Write │
│     with compaction        │   (primary/secondary)   │
├──────────────────────────────────────────────────────┤
│  Zero-Copy RESP Parser     │  Zero-Alloc RESP Writer │
│  (string_view into buffer) │ (stack buffers + memcpy)│
├──────────────────────────────────────────────────────┤
│           Software Pipeline (batch=16)               │
│           with N-ahead key prefetching               │
├──────────────────────────────────────────────────────┤
│  Command Dispatcher (integer-packed name comparison) │
├──────────────────────────────────────────────────────┤
│                    Database Layer                    │
│  ┌─────────────────────────────────────────────────┐ │
│  │          Robin Hood FlatMap (SoA layout)        │ │
│  │  ┌──────────┬──────────┬──────────┬───────────┐ │ │
│  │  │ metadata │  keys    │  values  │  hashes   │ │ │
│  │  │ (2B/slot)│(CompStr) │(variant) │ (uint32)  │ │ │
│  │  └──────────┴──────────┴──────────┴───────────┘ │ │
│  │  • Tag-based early rejection (7-bit hash tag)   │ │
│  │  • Incremental resizing (no latency spikes)     │ │
│  │  • SIEVE eviction (visited bit in metadata)     │ │
│  │                                                 │ │
│  └─────────────────────────────────────────────────┘ │
├──────────────────────────────────────────────────────┤
│  CompactString (16B SSO)   │  Slab Allocator         │
│  • Inline for ≤15 bytes    │  • 7 size classes       │
│  • 50% smaller than        │  • 1MB page allocation  │
│    std::string             │  • O(1) alloc/dealloc   │
│  • Binary-safe, move-only  │  • Zero fragmentation   │
└──────────────────────────────────────────────────────┘
```

---

## Key Design Decisions

### Async TCP Server with Asio Event Loop
- Single-threaded event loop using **Asio** (`io_context::run()`) — all I/O (accept, read, write) and maintenance timers run on one thread, eliminating all lock contention
- Each connection is managed via `std::shared_from_this()` and uses **async reads/writes** — the thread never blocks waiting for I/O
- `TCP_NODELAY` is enabled on every accepted socket to minimize latency
- A periodic **maintenance timer** fires via `asio::steady_timer` to run TTL cleanup and memory accounting without interrupting the event loop

### CompactString — 16-Byte SSO String
- Union-based string that stores strings ≤15 bytes inline without any heap allocation
- Heap-allocated strings use the slab allocator to prevent fragmentation
- Aligned to 16 bytes for SIMD-friendly access patterns
- At 16 Bytes, four keys can fit in a single 64-byte cache line, improving hash table probing performance
- Binary-safe, not null-terminated

### Robin Hood HashMap — Structure of Arrays
- **SoA layout**: Separate vectors for keys, values, metadata, and hashes 
  - Metadata scanning during probing touches only 2-byte entries → excellent cache utilization
  - Keys and values only accessed on match
- **2-byte metadata per slot**: 1 byte PSL + 1 byte tag (7 hash bits + 1 visited bit)
- **Tag-based early rejection**: Check PSL + 7-bit hash tag before comparing keys — avoids ~99% of false-positive key comparisons
- **Incremental resizing**: Entries migrated N-at-a-time per operation, spreading resize cost across requests
- **SIEVE eviction**: Clock-hand algorithm using the visited bit in metadata — approximates LRU with O(1) bookkeeping

### Command Dispatcher — Integer-Packed Lookup
- Command names (≤8 characters) are **uppercased and packed into a `uint64_t`** — dispatch is a single integer comparison instead of `strcmp`
- A fixed-size array of `CommandEntry` structs (max 32) is scanned linearly — fits entirely in L1 cache
- **Hottest commands registered first** (GET, SET) for early-exit on the most common path
- Argument count validation is inlined, with error formatting deferred to a pre-allocated stack buffer (no heap allocation on the error path)

### Zero-Copy RESP Parser
- Parses directly over `std::string_view` into the read buffer
- No allocations, no copies — arguments are views into the network buffer
- Handles partial reads and incomplete data gracefully

### Zero-Allocation RESP Writer
- Integer formatting via `std::to_chars` into stack-allocated buffers
- `reserveContiguous()` pre-allocates space in the LinearBuffer then writes directly
- No `std::string` construction or heap allocation on the write path

### Software Pipeline with Prefetching
- Batches up to 16 commands from the read buffer
- During execution, prefetches hash table metadata and keys for command `i + LOOKAHEAD`

### Double-Buffered Async Write
- Two response buffers per connection (primary + secondary)
- While the OS writes the primary buffer, responses accumulate in the secondary
- Swap-and-send on completion — no write stalls

### Slab Allocator
- 7 size classes (32B to 2048B) with free-list allocation
- 1MB page allocation to reduce `mmap` syscalls
- Fallback to `::operator new` for oversized allocations
- Allocations ≤2KB are O(1) (pop from free-list). Deallocations are O(1) (push to free-list).

---

## Supported Commands

| Command | Description |
|---------|-------------|
| `PING [message]` | Connection test |
| `ECHO message` | Echo back message |
| `SET key value [PX milliseconds]` | Set key-value with optional TTL |
| `GET key` | Retrieve value |
| `DEL key [key ...]` | Delete one or more keys |
| `EXISTS key [key ...]` | Check key existence |
| `INCR key` | Increment integer value |
| `DECR key` | Decrement integer value |
| `INCRBY key delta` | Increment by delta |
| `DECRBY key delta` | Decrement by delta |
| `EXPIRE key milliseconds` | Set TTL on key |
| `PERSIST key` | Remove TTL |
| `TTL key` | Get remaining TTL |

---

## Benchmarks

### Test Environment

| Component | Details |
|---|---|
| **CPU** | AMD Ryzen 7 4800H (8C/16T, 2.9 GHz base) |
| **RAM** | 16 GB |
| **L1 Data Cache** | 32 KiB × 8 |
| **L1 Instruction Cache** | 32 KiB × 8 |
| **L2 Cache** | 512 KiB × 8 |
| **L3 Cache** | 4096 KiB × 2|
| **OS** | Linux (Kubuntu 24.04 LTS) |
| **Benchmark Tool** | `memtier_benchmark` — 4 threads, 50 clients/thread, CPU-pinned via `taskset` |
| **Redis** | Port 6380 |
| **RapidKV** | Port 8080 |

> For a fair comparison, Redis was configured to run without persistence.
> Both servers were restarted between benchmark runs to ensure a clean state.
> Both servers were run on the same machine, with CPU affinity set to isolate them from the benchmark threads.

---

### 1. Baseline (No Pipeline)

> **Pipeline = 1, 1:1 SET:GET, 8B values, 100K keys** — True per-request latency floor. Both servers are network/syscall-bound at this depth.

| Metric | Redis | RapidKV | Δ Change |
|---|---|---|---|
| **Ops/sec** | 130,019 | 135,165 | +4.0% |
| **Avg Latency** | 1.549 ms | 1.478 ms | −4.6% |
| **p50 Latency** | 1.527 ms | 1.479 ms | −3.1% |
| **p99 Latency** | 3.055 ms | 1.567 ms | **−48.7%** |
| **p99.9 Latency** | 3.695 ms | 3.231 ms | −12.6% |

**Key takeaway:** At pipeline=1, throughput is nearly identical because both servers spend most of time in `read()`/`write()` syscalls. However, RapidKV's **p99 is 49% lower** — its tighter code path produces less jitter even when syscall-bound. This is the honest baseline: it shows RapidKV matches Redis in the worst case and never falls behind.

---

### 2. Realistic Mixed Workload (1:9 SET:GET)

> **Pipeline = 16, 64B values, 200K keys** — Production-like read-heavy scenario. Pre-seeded 200K keys.

| Metric | Redis | RapidKV | Δ Change |
|---|---|---|---|
| **Total Ops/sec** | 1,119,012 | 1,823,366 | **+63.0%** |
| **SET Ops/sec** | 111,901 | 182,337 | **+62.9%** |
| **GET Ops/sec** | 1,007,110 | 1,641,030 | **+62.9%** |
| **Avg Latency** | 2.856 ms | 1.750 ms | **−38.7%** |
| **p99 Latency** | 4.511 ms | 1.895 ms | **−58.0%** |
| **Bandwidth** | 49.3 MB/s | 80.3 MB/s | **+62.9%** |

---

### 3. Pipeline Scaling

> **1:1 SET:GET, 8B values, 100K keys** — Shows how throughput scales with pipeline depth.

| Pipeline | Redis Ops/s | RapidKV Ops/s | Redis Avg Lat | RapidKV Avg Lat | Redis p99 | RapidKV p99 | Speedup |
|---|---|---|---|---|---|---|---|
| **1** | 130,019 | 135,165 | 1.55 ms | 1.48 ms | 3.06 ms | 1.57 ms | 1.04× |
| **10** | 778,354 | 1,175,299 | 2.57 ms | 1.69 ms | 4.48 ms | 1.85 ms | 1.51× |
| **50** | 1,408,724 | 3,446,779 | 7.12 ms | 2.73 ms | 11.78 ms | 4.10 ms | **2.45×** |
| **100** | 1,558,692 | 4,050,247 | 12.77 ms | 4.86 ms | 19.07 ms | 7.55 ms | **2.60×** |
| **200** | 1,658,811 | 4,258,246 | 24.07 ms | 9.25 ms | 31.36 ms | 14.14 ms | **2.57×** |

**Key takeaway:** At pipeline=1 both are network-bound and equivalent. As pipeline depth grows, RapidKV dominates — Redis plateaus at ~1.6M ops/s while RapidKV peaks at **4.3M ops/s**. Even with such a significant difference in throughput, RapidKV maintains lower latency across all percentiles.

```
Ops/sec (thousands)

  4258 ┤                                            ★ RapidKV
  4050 ┤                                   ★
  3447 ┤                          ★
       ┤
  1659 ┤                                            ● Redis
  1559 ┤                                   ●
  1409 ┤                          ●
  1175 ┤                  ★
   778 ┤                  ●
   135 ┤        ★
   130 ┤        ●
       └────────┬────────┬────────┬────────┬────────┬────────
               1       10       50      100     200
                        Pipeline Depth
```

---

### 4. Extremely High Bandwidth

> **Pipeline = 64, 1:1 SET:GET, 2KB values, 100K keys** — Stress-tests I/O path with large payloads.

| Metric | Redis | RapidKV | Δ Change |
|---|---|---|---|
| **Total Ops/sec** | 651,238 | 1,305,176 | **+100.4%** |
| **Bandwidth** | 780 MB/s | 1,562 MB/s (1.52 GB/s) | **+100.0%** |
| **Avg Latency** | 19.77 ms | 9.80 ms | **−50.4%** |
| **p99 Latency** | 25.73 ms | 13.70 ms | **−46.8%** |

**Key takeaway:** With 2KB values, the double-buffered async write architecture keeps the TCP pipe saturated — RapidKV pushes **2× Redis's bandwidth**. A bandwidth of 1.52 GB/s (more than 10-Gigabit Ethernet can handle) is impressive for a single-threaded server on commodity hardware, and the fact that RapidKV achieves this while also maintaining **50% lower latency** is a testament to the efficiency of its I/O path and memory management.

---

### 5. Heap Allocation Stress (Slab Allocator)

> **Pipeline = 16, SET-only, 128B values, 42-char key prefix** — Keys and values both exceed SSO, exercising the slab allocator.

| Metric | Redis | RapidKV | Δ Change |
|---|---|---|---|
| **SET Ops/sec** | 900,795 | 1,323,604 | **+46.9%** |
| **Avg Latency** | 3.547 ms | 2.409 ms | **−32.1%** |
| **p99 Latency** | 5.311 ms | 2.799 ms | **−47.3%** |
| **Bandwidth** | 178.6 MB/s | 262.5 MB/s | **+46.9%** |

**Key takeaway:** Even when every key/value triggers a heap allocation, the slab allocator maintains **47% higher throughput** and **47% lower p99** vs Redis's `malloc`-based allocator.

---

### 6. GET-Only with Hardware Performance Counters
*pipeline=16, data-size=8 bytes, GET-only after pre-seeding 500K keys*

| Metric | Redis | RapidKV | Improvement |
|---|---|---|---|
| Ops/sec (GET) | 1,091,279 | **1,817,189** | **+66.5%** |
| Avg Latency | 2.926 ms | **1.756 ms** | −40.0% |
| p99 Latency | 4.703 ms | **1.927 ms** | **−59.0%** |

**Hardware counters (normalized per GET operation):**

| Counter | Redis | RapidKV | Improvement |
|---|---|---|---|
| Instructions/op | 5,421 | **2,144** | **−60%** |
| L1 dcache loads/op | 2,207 | **883** | **−60%** |
| L1 dcache misses/op | 53 | **44** | **−16%** |
| dTLB loads/op | 15.8 | **9.8** | **−38%** |
| dTLB misses/op | 0.48 | **0.21** | **−56%** |
| Cache misses/op | 20.6 | **17.7** | **−14%** |
| Cycles/op | 3,813 | **2,265** | **−41%** |

**Key takeaway:** RapidKV executes **60% fewer instructions**, **60% lower L1 cache loads** and incurs **56% fewer TLB misses** per GET operation. The compact data structures (16-byte CompactString, SoA FlatMap) have a dramatically smaller memory footprint.

---

### Data Structure Micro-Benchmarks

#### CompactString vs std::string

> Measured via Google Benchmark. `/0` = SSO-eligible (short strings), `/1` = heap-allocated (long strings).

| Operation | N | CompactString | std::string | Improvement |
|---|---|---|---|---|
| **Construction (SSO)** | 1M | 8.17 ms | 8.77 ms | 6.8% faster |
| **Construction (Heap)** | 1M | 114 ms | 413 ms | **3.6× faster** |
| **Access (SSO)** | 1M | 0.921 ms | 1.50 ms | **1.63× faster** |
| **Access (Heap)** | 1M | 0.864 ms | 1.51 ms | **1.75× faster** |
| **Modification** | 100K | 0.765 ms | 0.910 ms | 19% faster |
| **Move** | 100K | 0.313 ms | 1.81 ms | **5.8× faster** |

**Key takeaway:** CompactString is dramatically faster for heap construction (**3.6×**) and moves (**5.8×**). The move advantage directly benefits Robin Hood swaps during insertion.

#### FlatMap vs std::unordered_map

> Measured via Google Benchmark. FlatMap+CompactString is the configuration used in RapidKV.

| Operation | N | unordered_map | FlatMap (std::string) | FlatMap (CompactString) | Best Improvement |
|---|---|---|---|---|---|
| **Insertion** | 1M | 460 ms | 253 ms (1.8×) | 186 ms | **2.5× faster** |
| **Insertion** | 10M | 4,934 ms | 3,299 ms (1.5×) | 2,365 ms | **2.1× faster** |
| **Lookup** | 1M | 113 ms | 64.7 ms (1.7×) | 60.5 ms | **1.9× faster** |
| **Lookup** | 10M | 1,430 ms | 848 ms (1.7×) | 733 ms | **2.0× faster** |
| **Deletion** | 1M | 297 ms | 130 ms (2.3×) | 114 ms | **2.6× faster** |
| **Mixed** | 10M | 5,102 ms | 3,407 ms (1.5×) | 2,702 ms | **1.9× faster** |

**Key takeaway:** The SoA Robin Hood layout alone provides 1.5–2.3× improvement over `std::unordered_map`. Adding CompactString pushes it to **1.9–2.6× faster** across all operations. The combination of cache-friendly probing, 16-byte keys, and slab allocation compounds at every level.

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
| [RapidHash](https://github.com/nicbarker/rapidhash) | vendored | Fast hashing |
| [Google Test](https://github.com/google/googletest) | 1.15.2 | Unit testing (optional) |
| [Google Benchmark](https://github.com/google/benchmark) | 1.9.1 | Micro-benchmarks (optional) |

### Linux

```bash
# Clone
git clone https://github.com/shashank2602/RapidKV.git
cd RapidKV

# Build (Release)
mkdir build && cd build
cmake .. -DCMAKE_BUILD_TYPE=Release
cmake --build . -j$(nproc)

# Build with tests and benchmarks
cmake .. -DCMAKE_BUILD_TYPE=Release -DBUILD_EXTRAS=ON
cmake --build . -j$(nproc)

# Run
./RapidKV                     # default: 0.0.0.0:8080
./RapidKV ../config.txt       # custom config

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
git clone https://github.com/shashank2602/RapidKV.git
cd RapidKV

# Build (Release)
mkdir build && cd build
cmake ..
cmake --build . --config Release

# Build with tests and benchmarks
cmake .. -DBUILD_EXTRAS=ON
cmake --build . --config Release

# Run
.\Release\RapidKV.exe
.\Release\RapidKV.exe ..\config.txt

# Run tests
ctest -C Release --output-on-failure
```

### Configuration File Format

```
#RapidKV Configuration File

port 8080
bind 0.0.0.0
max_memory_usage 2147483648
maintenance_interval_ms 100
```



## Project Structure

```
RapidKV/
├── CMakeLists.txt
├── src/
│   ├── main.cpp
│   ├── server/          # Server (accept loop) + Connection (read/write/pipeline)
│   ├── protocol/        # RESP parser & writer
│   ├── commands/        # Command handlers & dispatcher
│   ├── storage/         # FlatMap, CompactString, SlabAllocator, Database
│   └── utility/         # LinearBuffer, InlineVector, Config, utility functions
├── inc/                 # Headers (mirrors src/ structure)
├── tests/               # Google Test unit + integration tests
│   ├── testCompactString.cpp
│   ├── testFlatMap.cpp
│   ├── testDatabase.cpp
│   ├── testRespProtocol.cpp
│   └── testIntegration.cpp
├── benchmarks/          # Google Benchmark microbenchmarks
│   ├── benchmarkCompactString.cpp
│   ├── benchmarkFlatMap.cpp
│   ├── benchmarkDatabase.cpp
│   └── benchmarkCommandDispatcher.cpp
└── config.conf          # Sample configuration
```


---

## Limitations

- **Single-threaded** — no multi-core scaling
- **String-only values** — no lists, sets, sorted sets, or hashes
- **No persistence** — all data is in-memory only (no RDB/AOF snapshots)
- **No replication or clustering**
- **No AUTH, ACL, or TLS support**
- **Limited command set** — 13 commands vs Redis's 400+
- **Little-endian only** — The CompactString SSO bit layout assumes little-endian architecture.

---

## License

This project is open source.