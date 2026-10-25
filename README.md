# RingCache

RingCache is a planned distributed, in-memory key-value cache engineered in C++17. It utilizes **consistent hashing** with virtual nodes to distribute keys predictably across multiple cache nodes while minimizing key remapping during cluster scaling or node churn.

> **Project Status: In Development**  
> Core C++17 consistent hash ring with virtual nodes, in-memory LRU cache, single-process cluster simulation, multi-process TCP cache daemon (`ringcache_node`), networked client router (`NetworkClusterRouter`), and interactive CLI (`ringcache_cli`) are implemented and verified.

---

## Problem Statement

Traditional distributed caching often relies on modulo hashing:

$$\text{node} = \text{hash}(\text{key}) \pmod N$$

While straightforward, adding or removing a node from an $N$-node cluster forces approximately $\frac{N}{N+1}$ of all keys to relocate. In high-throughput systems, this triggers severe cache miss storms, heavy database failover load, and latency spikes.

Consistent hashing addresses this challenge by mapping both cache keys and physical nodes to a circular hash ring ($[0, 2^{64} - 1]$). When a node is added or removed:
- Only keys belonging to the immediate successor node are redistributed ($\approx \frac{K}{N}$ keys on average, where $K$ is the total number of keys).
- Virtual nodes (vnodes) are introduced to prevent hash clustering, ensuring balanced load and uniform memory consumption across physical machines.

---

## Architecture Overview

RingCache is designed around modular, decoupled components:

```
[ Client / CLI (PUT / GET / DELETE / PING) ]
                     │
                     ▼
          ┌──────────────────────┐
          │ NetworkClusterRouter │  Consistent hash ring with virtual nodes;
          │    (Client Side)     │  routes to primary & R-1 successor replicas.
          └──────────┬───────────┘
                     │  TCP (Bounded JSON Wire Protocol)
                     ▼
          ┌──────────────────────┐
          │   TcpServer / Node   │  Asynchronous Asio worker pool;
          │ (ringcache_node.exe) │  bounded frames (<= 64 KB);
          └──────────┬───────────┘  zero storage locks during network I/O.
                     │
                     ▼
          ┌──────────────────────┐
          │      CacheNode       │  Thread-safe in-memory LRU cache engine
          │   (LruCache Store)   │  backed by std::list and std::unordered_map.
          └──────────────────────┘
```

1. **Consistent Hash Ring**: Maintains node positions across the 64-bit identifier space. Keys are assigned to the first node whose token is greater than or equal to the key's hash (wrapping around at the end of the ring).
2. **Virtual Nodes**: Each physical node registers multiple virtual nodes (tokens, default 150) across the ring to ensure statistically even key distribution.
3. **Storage Engine & LRU Eviction**: Thread-safe in-memory key-value store (`LruCache`) using an exact LRU (Least Recently Used) policy to discard oldest entries when capacity limits are reached.
4. **TCP Protocol & Framing**: Bounded newline-delimited JSON wire protocol over TCP using standalone Asio. Enforces a strict 64 KB maximum frame size and non-throwing error handling.
5. **Multi-Node Routing & Replication**: Orchestrates nodes using consistent hash routing, clockwise successor replication across $R$ nodes, and replica fallback reads (in-memory `ClusterRouter` and networked `NetworkClusterRouter`).
6. **Daemon & CLI Executables**: Standalone cache daemon process (`ringcache_node`) and cluster CLI client (`ringcache_cli`).

---

## Planned vs. Implemented Features

| Feature | Status | Notes |
| :--- | :--- | :--- |
| **CMake Build System & Tests** | Implemented | C++17 target-based CMake, `ringcache` library, and automated `ringcache_tests` |
| **Repository & CI Baseline** | Implemented | Documentation, issue templates, agent standards, GitHub Actions CI workflow |
| **Consistent Hash Ring** | Implemented | Thread-safe `HashRing` using `std::map` binary search ($O(\log(N \times V))$) and deterministic 64-bit hashing |
| **Virtual Nodes (vnodes)** | Implemented | Configurable vnodes per physical node (default 150) for uniform key partitioning |
| **LRU Cache Engine** | Implemented | Thread-safe `LruCache` with $O(1)$ `get`, `put`, and `remove` backed by `std::list` and `std::unordered_map` |
| **Thread-Safe Storage** | Implemented | Mutex synchronization ensuring zero data races across concurrent reads and mutations |
| **Multi-Node Replication** | Implemented | In-process multi-node cluster (`ClusterRouter`) with configurable successor replication factor $R$ and replica fallback read routing |
| **TCP Protocol & Multi-Process** | Implemented | Asynchronous TCP server (`TcpServer`), client router (`NetworkClusterRouter`), bounded JSON wire protocol, `ringcache_node` daemon, and `ringcache_cli` |
| **Node Failure Detection** | Planned | Heartbeat probes and dynamic ring membership updates |

---

## Prerequisites

Building RingCache requires:
- **C++ Compiler**: C++17 compliant (GCC 9+, Clang 10+, or MSVC 2019+)
- **Build System**: CMake 3.16 or higher
- **Build Tool**: Ninja (recommended) or GNU Make / MSBuild
- **Testing**: CTest (included with CMake)

---

## Build, Run, and Testing

RingCache uses standard, target-based CMake workflows:

### Building
Configure and build all targets (`ringcache` library, `ringcache_node` daemon, `ringcache_cli` client, and `ringcache_tests`):

```bash
# Configure the build directory (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build
```

*(On Windows with MinGW, append `-G Ninja -DCMAKE_CXX_COMPILER=g++` if your default environment requires it).*

### Running Multi-Process Cache Nodes

Launch cache node processes with custom identifiers, listen ports, and capacities:

```bash
# Terminal 1: Launch Node 1
./build/ringcache_node --id node-1 --host 127.0.0.1 --port 18001 --capacity 1000

# Terminal 2: Launch Node 2
./build/ringcache_node --id node-2 --host 127.0.0.1 --port 18002 --capacity 1000

# Terminal 3: Launch Node 3
./build/ringcache_node --id node-3 --host 127.0.0.1 --port 18003 --capacity 1000
```

### Interacting via the CLI

Use `ringcache_cli` to route requests across the cluster with consistent hashing and successor replication:

```bash
# Health-check / ping a node
./build/ringcache_cli ping --peers node-1=127.0.0.1:18001

# Store a key replicated across R=2 distinct physical nodes
./build/ringcache_cli put mykey "Hello RingCache" \
    --peers node-1=127.0.0.1:18001,node-2=127.0.0.1:18002,node-3=127.0.0.1:18003 \
    --replicas 2

# Retrieve a key (queries primary, falls back to replica if needed)
./build/ringcache_cli get mykey \
    --peers node-1=127.0.0.1:18001,node-2=127.0.0.1:18002,node-3=127.0.0.1:18003

# Delete a key from primary and all replicas
./build/ringcache_cli del mykey \
    --peers node-1=127.0.0.1:18001,node-2=127.0.0.1:18002,node-3=127.0.0.1:18003
```

### Testing
Execute the automated test suite using CTest:

```bash
# Execute test suite with verbose output on failure
ctest --test-dir build --output-on-failure
```

You can also run the test executable directly to inspect individual test cases (41 unit and integration tests covering consistent hashing, LRU eviction, cluster routing, bounded wire protocol, and multi-process TCP networking):

```bash
# Linux / macOS
./build/tests/ringcache_tests

# Windows
.\build\tests\ringcache_tests.exe
```

---

## Development Roadmap

- [x] **Milestone 1 (Foundation)**: Minimal CMake project skeleton, library target (`ringcache`), and automated test suite (`ringcache_tests`).
- [x] **Milestone 2 (Consistent Hashing)**: Consistent hashing ring implementation with virtual node distribution and automated test verification.
- [x] **Milestone 3 (Storage Engine)**: In-memory LRU cache engine (`LruCache`) with $O(1)$ operations, capacity bounding, and verified thread safety.
- [x] **Milestone 4 (Cluster Routing & Replication)**: In-process multi-node cluster router (`ClusterRouter`, `CacheNode`), consistent hash routing with configurable successor replication factor $R$, and fallback reads.
- [x] **Milestone 5 (Networking & Multi-Process Distribution)**: TCP network transport layer, bounded JSON wire protocol, `ringcache_node` daemon process, `ringcache_cli` client tool, and `NetworkClusterRouter` with successor replication and multi-process integration tests.
- [ ] **Milestone 6 (Clustering & Failure Detection)**: Multi-node cluster failure detection, heartbeat probes, and dynamic membership churn handling.

---

## Contributing

Contributions are welcome. Before contributing:
1. Review [AGENTS.md](AGENTS.md) for architectural constraints, code style, and verification guidelines.
2. Review [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md) to understand current design decisions and open questions.
3. Open an issue using [bug report](.github/ISSUE_TEMPLATE/bug_report.md) or [feature request](.github/ISSUE_TEMPLATE/feature_request.md) templates before proposing major architectural changes.
