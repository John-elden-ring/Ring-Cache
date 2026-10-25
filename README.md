# RingCache

RingCache is a planned distributed, in-memory key-value cache engineered in C++17. It utilizes **consistent hashing** with virtual nodes to distribute keys predictably across multiple cache nodes while minimizing key remapping during cluster scaling or node churn.

> **Project Status: In Development**  
> Core C++17 consistent hash ring with virtual nodes and automated test suite are implemented. Storage engine and networking components are planned for subsequent milestones.

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
[ Client Request (GET / SET / DEL) ]
                │
                ▼
      ┌──────────────────┐
      │ Consistent Hash  │  Maps key to ring position;
      │       Ring       │  identifies primary and replica nodes.
      └─────────┬────────┘
                │
         TCP Framing / RPC
                │
                ▼
     ┌─────────────────────┐
     │   Storage Engine    │  In-memory hash table (std::unordered_map)
     │   & LRU Eviction    │  backed by doubly-linked list for O(1) eviction.
     └─────────────────────┘
```

1. **Consistent Hash Ring**: Maintains node positions across the 64-bit identifier space. Keys are assigned to the first node whose token is greater than or equal to the key's hash (wrapping around at the end of the ring).
2. **Virtual Nodes**: Each physical node registers multiple virtual nodes (tokens) across the ring to ensure statistically even key distribution.
3. **Storage Engine & Eviction**: A thread-safe in-memory key-value store (`LruCache`) using an LRU (Least Recently Used) policy to discard oldest entries when capacity limits are reached.
4. **Networking & Framing (Planned)**: A lightweight TCP server utilizing length-prefixed binary framing for request and response commands (`GET`, `SET`, `DEL`), as proposed in [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md).
5. **Replication (Planned)**: Successor-based replication across $R$ successive nodes on the ring to preserve cache availability during transient node failures.

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
| **TCP Protocol & Framing** | Planned | Non-blocking socket communication with length-prefixed binary commands |
| **Multi-Node Replication** | Planned | Asynchronous / semi-synchronous successor replication |
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
Configure and build all targets (the `ringcache` library and `ringcache_tests` test runner):

```bash
# Configure the build directory (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build
```

*(On Windows with MinGW, append `-G Ninja -DCMAKE_CXX_COMPILER=g++` if your default environment requires it).*

### Running
The cache server daemon binary (e.g., `ringcache_server`) will be introduced in Milestone 3. The current milestone provides the foundational `ringcache` C++17 library target and automated test suite.

### Testing
Execute the automated test suite using CTest:

```bash
# Execute test suite with verbose output on failure
ctest --test-dir build --output-on-failure
```

You can also run the test executable directly to inspect individual test cases:

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
- [ ] **Milestone 4 (Networking & Framing)**: Network transport layer, binary framing, CLI client, and server executable.
- [ ] **Milestone 5 (Clustering & Replication)**: Multi-node clustering, replication factor, and node membership churn handling.

---

## Contributing

Contributions are welcome. Before contributing:
1. Review [AGENTS.md](AGENTS.md) for architectural constraints, code style, and verification guidelines.
2. Review [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md) to understand current design decisions and open questions.
3. Open an issue using [bug report](.github/ISSUE_TEMPLATE/bug_report.md) or [feature request](.github/ISSUE_TEMPLATE/feature_request.md) templates before proposing major architectural changes.
