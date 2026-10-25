#!/usr/bin/env python3
"""
RingCache Benchmark and Experiment Automation Runner.

Automates lifecycle management of multi-process RingCache clusters,
verifies node readiness, executes parameterized benchmark runs via ringcache_bench,
and collects performance metrics in machine-readable CSV format.
"""

import argparse
import os
import platform
import socket
import subprocess
import sys
import time
from typing import List, Optional, Tuple


def find_binary(name: str) -> Optional[str]:
    candidates = [
        f"{name}.exe" if platform.system() == "Windows" else name,
        os.path.join("build", f"{name}.exe" if platform.system() == "Windows" else name),
        os.path.join("..", "build", f"{name}.exe" if platform.system() == "Windows" else name),
        os.path.join("..", f"{name}.exe" if platform.system() == "Windows" else name),
    ]
    for c in candidates:
        if os.path.exists(c):
            return os.path.abspath(c)
    return None


def wait_for_node(port: int, timeout_sec: float = 5.0) -> bool:
    deadline = time.time() + timeout_sec
    while time.time() < deadline:
        try:
            s = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
            s.settimeout(0.5)
            s.connect(("127.0.0.1", port))
            # Send PING frame
            s.sendall(b'{"op":"PING"}\n')
            data = s.recv(1024)
            s.close()
            if b'"status":"OK"' in data or b'"status": "OK"' in data:
                return True
        except Exception:
            pass
        time.sleep(0.05)
    return False


class ManagedCluster:
    def __init__(self, node_bin: str, num_nodes: int, base_port: int = 19100, capacity: int = 5000):
        self.node_bin = node_bin
        self.num_nodes = num_nodes
        self.base_port = base_port
        self.capacity = capacity
        self.processes: List[subprocess.Popen] = []
        self.endpoints: List[Tuple[str, int]] = []

    def start(self):
        print(f"--> Spawning cluster with {self.num_nodes} node(s)...")
        for i in range(1, self.num_nodes + 1):
            node_id = f"node-{i}"
            port = self.base_port + i
            cmd = [
                self.node_bin,
                "--id", node_id,
                "--host", "127.0.0.1",
                "--port", str(port),
                "--capacity", str(self.capacity),
                "--threads", "2"
            ]
            p = subprocess.Popen(
                cmd,
                stdout=subprocess.DEVNULL,
                stderr=subprocess.DEVNULL
            )
            self.processes.append(p)
            self.endpoints.append((node_id, port))

        for node_id, port in self.endpoints:
            if not wait_for_node(port, timeout_sec=5.0):
                self.stop()
                raise RuntimeError(f"Node {node_id} on port {port} failed to become ready.")
        print(f"--> All {self.num_nodes} nodes are healthy and responding.")

    def get_peers_arg(self) -> str:
        return ",".join(f"{nid}=127.0.0.1:{port}" for nid, port in self.endpoints)

    def stop(self):
        print(f"--> Stopping {len(self.processes)} node process(es)...")
        for p in self.processes:
            try:
                p.terminate()
                p.wait(timeout=2)
            except Exception:
                try:
                    p.kill()
                except Exception:
                    pass
        self.processes.clear()
        self.endpoints.clear()


def run_single_benchmark(bench_bin: str,
                         peers_arg: str,
                         requests: int,
                         clients: int,
                         keys: int,
                         val_size: int,
                         ratio: str,
                         rep: int,
                         csv_path: str):
    cmd = [
        bench_bin,
        "--peers", peers_arg,
        "--requests", str(requests),
        "--clients", str(clients),
        "--keys", str(keys),
        "--val-size", str(val_size),
        "--ratio", ratio,
        "--rep", str(rep),
        "--out", csv_path
    ]
    print(f"\n[RUN] {' '.join(cmd)}")
    res = subprocess.run(cmd)
    if res.returncode != 0:
        print(f"[WARN] Benchmark exited with code {res.returncode}")


def main():
    parser = argparse.ArgumentParser(description="RingCache Benchmark & Experiment Suite")
    parser.add_argument("--quick", action="store_true", help="Run a quick local smoke benchmark")
    parser.add_argument("--matrix", action="store_true", help="Run full matrix of experiments")
    parser.add_argument("--nodes", type=int, default=3, help="Number of nodes to spawn (default: 3)")
    parser.add_argument("--requests", type=int, default=10000, help="Total requests per run (default: 10000)")
    parser.add_argument("--clients", type=int, default=4, help="Client concurrency threads (default: 4)")
    parser.add_argument("--keys", type=int, default=1000, help="Key count (default: 1000)")
    parser.add_argument("--val-size", type=int, default=64, help="Value size in bytes (default: 64)")
    parser.add_argument("--ratio", type=str, default="80:20:0", help="Workload ratio GET:PUT:DEL (default: 80:20:0)")
    parser.add_argument("--rep", type=int, default=1, help="Replication factor R (default: 1)")
    parser.add_argument("--out", type=str, default="benchmark_results.csv", help="Output CSV path (default: benchmark_results.csv)")

    args = parser.parse_args()

    node_bin = find_binary("ringcache_node")
    bench_bin = find_binary("ringcache_bench")

    if not node_bin or not os.path.exists(node_bin):
        sys.exit("Error: Could not locate ringcache_node binary. Please build the project first.")
    if not bench_bin or not os.path.exists(bench_bin):
        sys.exit("Error: Could not locate ringcache_bench binary. Please build the project first.")

    print(f"Discovered Binaries:")
    print(f"  ringcache_node  : {node_bin}")
    print(f"  ringcache_bench : {bench_bin}")
    print(f"Output CSV path   : {os.path.abspath(args.out)}")

    if args.quick:
        # Quick verification run
        cluster = ManagedCluster(node_bin, num_nodes=3, base_port=19200)
        try:
            cluster.start()
            run_single_benchmark(
                bench_bin=bench_bin,
                peers_arg=cluster.get_peers_arg(),
                requests=2000,
                clients=2,
                keys=500,
                val_size=64,
                ratio="80:20:0",
                rep=1,
                csv_path=args.out
            )
        finally:
            cluster.stop()
        print("\nQuick benchmark finished successfully.")
        return

    if args.matrix:
        print("\n=======================================================")
        print("  Running Full Benchmark Matrix Experiments")
        print("=======================================================")

        # Experiment 1: Concurrency Scaling (C in [1, 2, 4, 8]) on 3 nodes (R=1)
        print("\n>>> Experiment 1: Concurrency Scaling (C=1, 2, 4, 8)")
        cluster = ManagedCluster(node_bin, num_nodes=3, base_port=19210)
        try:
            cluster.start()
            for c in [1, 2, 4, 8]:
                run_single_benchmark(
                    bench_bin=bench_bin,
                    peers_arg=cluster.get_peers_arg(),
                    requests=5000,
                    clients=c,
                    keys=1000,
                    val_size=64,
                    ratio="80:20:0",
                    rep=1,
                    csv_path=args.out
                )
        finally:
            cluster.stop()

        # Experiment 2: Replication Cost (R=1 vs R=2) on 3 nodes
        print("\n>>> Experiment 2: Replication Cost (R=1 vs R=2)")
        cluster = ManagedCluster(node_bin, num_nodes=3, base_port=19220)
        try:
            cluster.start()
            for r in [1, 2]:
                run_single_benchmark(
                    bench_bin=bench_bin,
                    peers_arg=cluster.get_peers_arg(),
                    requests=5000,
                    clients=4,
                    keys=1000,
                    val_size=64,
                    ratio="50:50:0",
                    rep=r,
                    csv_path=args.out
                )
        finally:
            cluster.stop()

        # Experiment 3: Workload Mix (Read-Heavy vs Balanced vs Write-Heavy)
        print("\n>>> Experiment 3: Workload Mix (Read-Heavy vs Balanced vs Write-Heavy)")
        cluster = ManagedCluster(node_bin, num_nodes=3, base_port=19230)
        try:
            cluster.start()
            for mix in ["90:10:0", "50:50:0", "10:90:0"]:
                run_single_benchmark(
                    bench_bin=bench_bin,
                    peers_arg=cluster.get_peers_arg(),
                    requests=5000,
                    clients=4,
                    keys=1000,
                    val_size=64,
                    ratio=mix,
                    rep=1,
                    csv_path=args.out
                )
        finally:
            cluster.stop()

        print("\nAll matrix experiments completed. Results written to:", args.out)
        return

    # Standard custom run
    cluster = ManagedCluster(node_bin, num_nodes=args.nodes, base_port=19240)
    try:
        cluster.start()
        run_single_benchmark(
            bench_bin=bench_bin,
            peers_arg=cluster.get_peers_arg(),
            requests=args.requests,
            clients=args.clients,
            keys=args.keys,
            val_size=args.val_size,
            ratio=args.ratio,
            rep=args.rep,
            csv_path=args.out
        )
    finally:
        cluster.stop()


if __name__ == "__main__":
    main()
