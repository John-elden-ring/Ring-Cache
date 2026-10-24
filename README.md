# RingCache

RingCache is a planned distributed, in-memory key-value cache engineered in C++17. It utilizes **consistent hashing** with virtual nodes to distribute keys predictably across multiple cache nodes while minimizing key remapping during cluster scaling or node churn.

> **Project Status: Proposed / Pre-Alpha**  
> The architecture and design specifications are currently being established. No runtime cache binaries or networking components are implemented yet.

---

## Problem Statement

Traditional distributed caching often relies on modulo hashing:

$$\text{node} = \text{hash}(\text{key}) \pmod N$$

While straightforward, adding or removing a node from an $N$-node cluster forces approximately $\frac{N}{N+1}$ of all keys to relocate. In high-throughput systems, this triggers severe cache miss storms, heavy database failover load, and latency spikes.

Consistent hashing addresses this challenge by mapping both cache keys and physical nodes to a circular hash ring ($[0, 2^{32} - 1]$ or $[0, 2^{64} - 1]$). When a node is added or removed:
- Only keys belonging to the immediate successor node are redistributed ($\approx \frac{K}{N}$ keys on average, where $K$ is the total number of keys).
- Virtual nodes (vnodes) are introduced to prevent hash clustering, ensuring balanced load and uniform memory consumption across physical machines.

---

## Proposed Architecture

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

1. **Consistent Hash Ring**: Maintains node positions across the identifier space. Keys are assigned to the first node whose token is greater than or equal to the key's hash (wrapping around at the end of the ring).
2. **Virtual Nodes**: Each physical node registers multiple virtual nodes (tokens) across the ring to ensure statistically even key distribution.
3. **Storage Engine & Eviction**: A thread-safe in-memory key-value store using an LRU (Least Recently Used) policy to discard oldest entries when capacity limits are reached.
4. **Networking & Framing**: A lightweight TCP server utilizing length-prefixed binary framing for request and response commands (`GET`, `SET`, `DEL`), as proposed in [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md).
5. **Replication (Planned)**: Successor-based replication across $R$ successive nodes on the ring to preserve cache availability during transient node failures.

---

## Planned vs. Implemented Features

| Feature | Status | Notes |
| :--- | :--- | :--- |
| **Consistent Hash Ring** | Planned | Ring structure and binary search lookup (`std::map` or sorted vector) |
| **Virtual Nodes (vnodes)** | Planned | Configurable vnode factor per physical node |
| **LRU Cache Engine** | Planned | $O(1)$ lookup, insertion, and eviction with memory bounding |
| **Thread-Safe Storage** | Planned | Reader-writer locks (`std::shared_mutex`) or lock striping |
| **TCP Protocol & Framing** | Planned | Non-blocking socket communication with length-prefixed binary commands |
| **Multi-Node Replication** | Planned | Asynchronous / semi-synchronous successor replication |
| **Node Failure Detection** | Planned | Heartbeat probes and dynamic ring membership updates |
| **Repository & CI Baseline** | Implemented (Baseline) | Documentation, issue templates, agent standards, CI workflow skeleton |

---

## Prerequisites

Building RingCache will require:
- **C++ Compiler**: C++17 compliant (GCC 9+, Clang 10+, or MSVC 2019+)
- **Build System**: CMake 3.16 or higher
- **Build Tool**: Ninja (recommended) or GNU Make
- **Testing**: CTest / GoogleTest or Catch2 (to be integrated in Milestone 1)

---

## Build, Run, and Testing (Planned)

> **Current Repository State**: The repository currently contains documentation, engineering guidelines, and GitHub workflows. The core `CMakeLists.txt` build configuration and target implementations are planned for Milestone 1. Invoking `cmake` or build tools before Milestone 1 will fail with `CMakeLists.txt not found`.

### Planned Build Instructions
Once `CMakeLists.txt` is introduced in Milestone 1, standard CMake commands will configure and build the project:

```bash
# Configure the build directory (Release mode)
cmake -B build -DCMAKE_BUILD_TYPE=Release

# Compile all targets
cmake --build build
```

### Planned Run Instructions
The cache server binary (e.g., `ringcache_server`) will be introduced in Milestone 3. Execution instructions, configuration file formats, and CLI parameter specifications (such as listening port, memory capacity, and peer addresses) will be documented with validated examples once the executable target is implemented.

### Planned Testing Instructions
Once test targets are defined in `CMakeLists.txt`, tests will be executed via CTest:

```bash
# Execute test suite with verbose output on failure
ctest --test-dir build --output-on-failure
```

---

## Development Roadmap

- [ ] **Milestone 1**: Minimal CMake skeleton, unit test harness, and single-node in-memory storage engine with LRU eviction and thread safety.
- [ ] **Milestone 2**: Consistent hashing ring implementation with virtual node distribution benchmarks.
- [ ] **Milestone 3**: Network transport layer, binary framing, CLI client, and server executable.
- [ ] **Milestone 4**: Multi-node clustering, replication factor, and node membership churn handling.

---

## Contributing

Contributions are welcome. Before contributing:
1. Review [AGENTS.md](AGENTS.md) for architectural constraints, code style, and verification guidelines.
2. Review [docs/DESIGN_DECISIONS.md](docs/DESIGN_DECISIONS.md) to understand current design decisions and open questions.
3. Open an issue using [bug report](.github/ISSUE_TEMPLATE/bug_report.md) or [feature request](.github/ISSUE_TEMPLATE/feature_request.md) templates before proposing major architectural changes.
