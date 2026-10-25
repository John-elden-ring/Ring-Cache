# RingCache Benchmarking, Failure Testing, and Performance Analysis

This document outlines the benchmarking methodology, failure resilience characteristics, measurement protocols, and instructions for reproducing performance experiments on **RingCache**.

---

## 1. Benchmarking Harness Architecture

The benchmark executable (`ringcache_bench`) provides a standalone load generator capable of stressing multi-process RingCache clusters over TCP.

### 1.1 Connection & Concurrency Model
To accurately benchmark the server-side multi-threaded TCP event loops (`TcpServer`) without artificial client-side contention:
- Each concurrent client worker thread instantiates its own `ringcache::NetworkClusterRouter`.
- Each router creates dedicated TCP client sockets (`NetworkClient`) to each node in the cluster.
- This creates $C \times N$ active TCP connections across the cluster (where $C$ is the client thread count and $N$ is the number of physical nodes).
- Workloads are uniformly partitioned across worker threads: $N_{\text{ops}} / C$ requests per thread.

### 1.2 Command-Line Options
```text
Usage: ringcache_bench --peers <node1=ip:port,...> [options]

Options:
  --peers <list>      Comma-separated node list (e.g. n1=127.0.0.1:8001,n2=127.0.0.1:8002) [REQUIRED]
  --requests <n>      Total operations across all client threads (default: 10000)
  --clients <n>       Number of concurrent client threads (default: 4)
  --keys <n>          Size of distinct key pool (default: 1000)
  --val-size <n>      Payload size in bytes for values (default: 64)
  --ratio <r:w:d>     Workload ratio GET:PUT:DEL (default: 80:20:0)
  --rep <factor>      Replication factor R (default: 1)
  --timeout <ms>      Per-request socket timeout in milliseconds (default: 2000)
  --warmup <n>        Keys to pre-populate before timing (default: min(keys, 1000))
  --out <csv_path>    Path to append machine-readable CSV benchmark results
  --help, -h          Show this help message
```

---

## 2. Measurement Methodology

### 2.1 Monotonic Timing
- All wall-clock durations and individual operation latencies are captured using `std::chrono::steady_clock` (guaranteed monotonic across all platforms).
- Individual request latencies are recorded in thread-local storage in microseconds ($\mu\text{s}$) with zero lock synchronization during the benchmark run.

### 2.2 Metrics Calculated
- **Completed Operations**: Total scheduled, total completed, successful, and failed operations (broken down by `GET`, `PUT`, `DEL`).
- **Throughput**:
  $$\text{Throughput} = \frac{\text{Total Operations}}{\Delta t_{\text{wall\_clock}}}$$
  $$\text{Successful Throughput} = \frac{\text{Successful Operations}}{\Delta t_{\text{wall\_clock}}}$$
- **Latency Percentiles**:
  For an array of $N$ sorted latencies $L$ (0-indexed):
  $$\text{rank}(P) = \left\lfloor \frac{P}{100} \times (N - 1) \right\rfloor$$
  $$\text{percentile}(P) = L[\text{rank}(P)]$$
  - Reported metrics: $\min$, $\text{mean}$, $p50$ (median), $p95$, $p99$, $\max$.

### 2.3 Warm-Up Phase
To ensure that read workloads measure actual cache hit performance rather than trivial key misses, `ringcache_bench` supports an automatic `--warmup <n>` phase that pre-populates keys across the cluster before measurement begins.

---

## 3. Failure Testing & Resilience Semantics

Automated failure tests in `tests/test_failure_resilience.cpp` verify the exact behavior of RingCache under node crashes, network drops, and restarts.

### 3.1 Read Resilience via Replica Fallback
- **Scenario**: A 3-node cluster configured with replication factor $R=2$. Keys are written across primary and clockwise replica nodes. The primary process for a subset of keys is abruptly terminated (`SIGTERM` / `TerminateProcess`).
- **Observed & Verified Behavior**: `NetworkClusterRouter::get` probes the assigned primary node. Upon connection drop or timeout, it transparently falls back to the clockwise replica over TCP. Reads succeed without data loss or application crash as long as at least one replica survives on the ring.

### 3.2 Write Failure Under Primary Node Crash (Phase 1 Limitation)
- **Scenario**: A node is killed while write (`PUT`) traffic is active.
- **Observed & Verified Behavior**: In Phase 1 static ring routing, writes directed at keys whose primary was the killed node fail (`putWithDetails` returns `success = false`, `writtenCount = 0`).
- **Architectural Reason**: In Phase 1, the cluster topology is static. The router does *not* automatically elect a new primary or mutate the consistent hash ring when a primary node becomes unreachable. Writes directed to surviving primary nodes succeed and propagate to surviving replicas.

### 3.3 Node Restart and Client Reconnection
- **Scenario**: A node daemon is stopped and later restarted on the same TCP port.
- **Observed & Verified Behavior**: During downtime, operations to the node fail cleanly with descriptive error messages. Upon restart, `NetworkClient`'s reconnection logic automatically establishes a new socket connection on the subsequent operation without requiring client restarts or throwing uncaught exceptions.

---

## 4. Reproducing Experiments

### 4.1 Automated Experiment Runner (`scripts/run_benchmarks.py`)

The included Python script automates cluster deployment, health checking, benchmark execution, and CSV metric collection.

#### Quick Smoke Test
```powershell
python scripts/run_benchmarks.py --quick
```

#### Full Matrix Experiments
Runs concurrency scaling ($C \in \{1, 2, 4, 8\}$), replication costs ($R=1$ vs $R=2$), and workload mix comparisons:
```powershell
python scripts/run_benchmarks.py --matrix --out benchmark_results.csv
```

#### Custom Cluster Benchmark
```powershell
python scripts/run_benchmarks.py --nodes 3 --clients 4 --requests 10000 --ratio 80:20:0 --rep 1 --out my_results.csv
```

### 4.2 Manual Step-by-Step Reproduction

1. **Build the targets**:
   ```powershell
   cmake --build build
   ```

2. **Start the nodes in separate terminals**:
   ```powershell
   # Terminal 1
   ./build/ringcache_node.exe --id node-1 --port 18801 --capacity 5000 --threads 2

   # Terminal 2
   ./build/ringcache_node.exe --id node-2 --port 18802 --capacity 5000 --threads 2

   # Terminal 3
   ./build/ringcache_node.exe --id node-3 --port 18803 --capacity 5000 --threads 2
   ```

3. **Run the benchmark executable**:
   ```powershell
   ./build/ringcache_bench.exe --peers node-1=127.0.0.1:18801,node-2=127.0.0.1:18802,node-3=127.0.0.1:18803 `
       --requests 10000 `
       --clients 4 `
       --keys 1000 `
       --ratio 80:20:0 `
       --rep 1 `
       --out benchmark_results.csv
   ```

---

## 5. Machine-Readable CSV Format

The benchmark output CSV includes all configuration parameters and performance metrics:

| Column | Description |
| :--- | :--- |
| `timestamp_iso` | ISO-8601 UTC timestamp of the benchmark run |
| `nodes` | Number of physical cluster nodes |
| `replication` | Configured replication factor $R$ |
| `concurrency` | Number of concurrent client worker threads |
| `total_requests` | Total operations scheduled |
| `key_count` | Distinct key pool size |
| `val_size_bytes` | Payload size of values in bytes |
| `ratio` | Workload ratio (`GET:PUT:DEL`) |
| `duration_sec` | Total wall-clock duration in seconds |
| `total_ops` | Total operations completed |
| `successful_ops` | Total operations succeeded |
| `failed_ops` | Total operations failed |
| `throughput_ops_sec` | Throughput in operations per second |
| `p50_us` | 50th percentile (median) latency in microseconds |
| `p95_us` | 95th percentile latency in microseconds |
| `p99_us` | 99th percentile latency in microseconds |
| `min_us` | Minimum observed latency in microseconds |
| `mean_us` | Mean observed latency in microseconds |
| `max_us` | Maximum observed latency in microseconds |

---

## 6. System Limitations & Performance Considerations

1. **Loopback Single-Host Evaluation**: Current benchmarks run on loopback interfaces (`127.0.0.1`) sharing host CPU, RAM, and OS network buffers. Results reflect software efficiency and concurrency scaling, but do not capture real-world cross-datacenter physical link latency or packet loss.
2. **Text-Based JSON Protocol**: RingCache currently employs newline-delimited JSON frames (`nlohmann::json`). JSON serialization and parsing introduce modest CPU overhead compared to binary protocols (e.g., RESP or Protocol Buffers).
3. **Lock Granularity**: Local node storage is protected by `LruCache`'s internal mutex. High thread counts per node may experience lock contention on write-heavy workloads. Partitioned/striped locking is deferred to future optimization milestones.
4. **Static Membership**: As designed in Phase 1 (ADR-007), node departure or failure does not automatically trigger hash ring rebalancing or promote replicas to primary for writes.
