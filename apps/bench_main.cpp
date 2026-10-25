#include "ringcache/network_cluster_router.hpp"
#include "ringcache/version.hpp"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <cstdint>
#include <ctime>
#include <fstream>
#include <iomanip>
#include <iostream>
#include <numeric>
#include <random>
#include <sstream>
#include <string>
#include <string_view>
#include <thread>
#include <vector>

namespace {

struct BenchmarkConfig {
    std::string peers;
    std::size_t totalRequests{10000};
    std::size_t concurrency{4};
    std::size_t keyCount{1000};
    std::size_t valueSizeBytes{64};
    std::size_t replicationFactor{1};
    std::chrono::milliseconds timeout{2000};
    std::size_t warmupKeys{1000};
    std::string csvPath;

    // Ratios: GET, PUT, DEL
    int getWeight{80};
    int putWeight{20};
    int delWeight{0};
};

struct PeerEndpoint {
    std::string id;
    std::string host;
    uint16_t port{0};
};

bool parsePeer(const std::string& peerStr, PeerEndpoint& out) {
    auto eqPos = peerStr.find('=');
    if (eqPos == std::string::npos) return false;
    out.id = peerStr.substr(0, eqPos);

    auto colonPos = peerStr.find(':', eqPos + 1);
    if (colonPos == std::string::npos) return false;
    out.host = peerStr.substr(eqPos + 1, colonPos - (eqPos + 1));
    out.port = static_cast<uint16_t>(std::stoul(peerStr.substr(colonPos + 1)));
    return true;
}

std::vector<PeerEndpoint> parsePeers(const std::string& peersArg) {
    std::vector<PeerEndpoint> peers;
    std::stringstream ss(peersArg);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        PeerEndpoint ep;
        if (parsePeer(item, ep)) {
            peers.push_back(ep);
        }
    }
    return peers;
}

bool parseRatio(const std::string& ratioStr, int& getW, int& putW, int& delW) {
    // Expected format: R:W:D, e.g. 80:20:0
    std::stringstream ss(ratioStr);
    std::string sGet, sPut, sDel;
    if (std::getline(ss, sGet, ':') && std::getline(ss, sPut, ':') && std::getline(ss, sDel, ':')) {
        try {
            getW = std::stoi(sGet);
            putW = std::stoi(sPut);
            delW = std::stoi(sDel);
            return (getW + putW + delW) > 0;
        } catch (...) {
            return false;
        }
    }
    return false;
}

void printUsage(std::string_view progName) {
    std::cout << "RingCache Benchmark Tool v" << ringcache::getVersionString() << "\n"
              << "Usage: " << progName << " --peers <node1=ip:port,...> [options]\n\n"
              << "Options:\n"
              << "  --peers <list>      Comma-separated node list (e.g. n1=127.0.0.1:8001,n2=127.0.0.1:8002) [REQUIRED]\n"
              << "  --requests <n>      Total operations across all client threads (default: 10000)\n"
              << "  --clients <n>       Number of concurrent client threads (default: 4)\n"
              << "  --keys <n>          Size of distinct key pool (default: 1000)\n"
              << "  --val-size <n>      Payload size in bytes for values (default: 64)\n"
              << "  --ratio <r:w:d>     Workload ratio GET:PUT:DEL (default: 80:20:0)\n"
              << "  --rep <factor>      Replication factor R (default: 1)\n"
              << "  --timeout <ms>      Per-request socket timeout in milliseconds (default: 2000)\n"
              << "  --warmup <n>        Keys to pre-populate before timing (default: min(keys, 1000))\n"
              << "  --out <csv_path>    Path to append machine-readable CSV benchmark results\n"
              << "  --help, -h          Show this help message\n";
}

std::string generatePayload(std::size_t size) {
    static const char charset[] = "0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz";
    std::string s(size, 'x');
    for (std::size_t i = 0; i < size; ++i) {
        s[i] = charset[i % (sizeof(charset) - 1)];
    }
    return s;
}

std::string getIsoTimestamp() {
    auto now = std::chrono::system_clock::now();
    std::time_t tt = std::chrono::system_clock::to_time_t(now);
    std::tm tmBuf{};
#ifdef _WIN32
    gmtime_s(&tmBuf, &tt);
#else
    gmtime_r(&tt, &tmBuf);
#endif
    std::ostringstream oss;
    oss << std::put_time(&tmBuf, "%Y-%m-%dT%H:%M:%SZ");
    return oss.str();
}

struct LatencyStats {
    uint64_t minUs{0};
    uint64_t meanUs{0};
    uint64_t p50Us{0};
    uint64_t p95Us{0};
    uint64_t p99Us{0};
    uint64_t maxUs{0};
};

LatencyStats computeLatencyStats(std::vector<uint64_t>& latencies) {
    LatencyStats stats;
    if (latencies.empty()) {
        return stats;
    }

    std::sort(latencies.begin(), latencies.end());
    const std::size_t n = latencies.size();

    stats.minUs = latencies.front();
    stats.maxUs = latencies.back();

    uint64_t sum = std::accumulate(latencies.begin(), latencies.end(), 0ULL);
    stats.meanUs = sum / n;

    // Nearest rank index: floor(P/100 * (N - 1))
    auto rank = [n](double p) -> std::size_t {
        if (n <= 1) return 0;
        double idx = (p / 100.0) * static_cast<double>(n - 1);
        return static_cast<std::size_t>(std::floor(idx));
    };

    stats.p50Us = latencies[rank(50.0)];
    stats.p95Us = latencies[rank(95.0)];
    stats.p99Us = latencies[rank(99.0)];

    return stats;
}

struct ThreadResult {
    std::size_t successOps{0};
    std::size_t failedOps{0};

    std::size_t getSuccess{0};
    std::size_t getFail{0};
    std::size_t putSuccess{0};
    std::size_t putFail{0};
    std::size_t delSuccess{0};
    std::size_t delFail{0};

    std::vector<uint64_t> allLatenciesUs;
    std::vector<uint64_t> getLatenciesUs;
    std::vector<uint64_t> putLatenciesUs;
    std::vector<uint64_t> delLatenciesUs;
};

void runWorker(std::size_t workerId,
               std::size_t opsToRun,
               const BenchmarkConfig& config,
               const std::vector<PeerEndpoint>& peers,
               const std::string& payload,
               ThreadResult& outResult) {
    outResult.allLatenciesUs.reserve(opsToRun);

    // Each thread instantiates its own NetworkClusterRouter with dedicated sockets
    ringcache::NetworkClusterRouter router(
        config.replicationFactor,
        ringcache::HashRing::DEFAULT_VNODES,
        config.timeout
    );

    for (const auto& p : peers) {
        router.addNode(p.id, p.host, p.port);
    }

    std::mt19937_64 rng(1337 + workerId * 7919);
    std::uniform_int_distribution<std::size_t> keyDist(0, config.keyCount - 1);
    std::uniform_int_distribution<int> opDist(0, config.getWeight + config.putWeight + config.delWeight - 1);

    for (std::size_t i = 0; i < opsToRun; ++i) {
        std::size_t keyIdx = keyDist(rng);
        std::string key = "bench_key_" + std::to_string(keyIdx);
        int opRoll = opDist(rng);

        auto t0 = std::chrono::steady_clock::now();
        bool ok = false;

        if (opRoll < config.getWeight) {
            // GET
            auto val = router.get(key);
            ok = val.has_value();
            auto t1 = std::chrono::steady_clock::now();
            uint64_t us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
            );
            outResult.getLatenciesUs.push_back(us);
            outResult.allLatenciesUs.push_back(us);
            if (ok) {
                outResult.getSuccess++;
            } else {
                outResult.getFail++;
            }
        } else if (opRoll < (config.getWeight + config.putWeight)) {
            // PUT
            ok = router.put(key, payload);
            auto t1 = std::chrono::steady_clock::now();
            uint64_t us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
            );
            outResult.putLatenciesUs.push_back(us);
            outResult.allLatenciesUs.push_back(us);
            if (ok) {
                outResult.putSuccess++;
            } else {
                outResult.putFail++;
            }
        } else {
            // DEL
            ok = router.del(key);
            auto t1 = std::chrono::steady_clock::now();
            uint64_t us = static_cast<uint64_t>(
                std::chrono::duration_cast<std::chrono::microseconds>(t1 - t0).count()
            );
            outResult.delLatenciesUs.push_back(us);
            outResult.allLatenciesUs.push_back(us);
            if (ok) {
                outResult.delSuccess++;
            } else {
                outResult.delFail++;
            }
        }

        if (ok) {
            outResult.successOps++;
        } else {
            outResult.failedOps++;
        }
    }
}

} // namespace

int main(int argc, char* argv[]) {
    BenchmarkConfig config;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--peers" && i + 1 < argc) {
            config.peers = argv[++i];
        } else if (arg == "--requests" && i + 1 < argc) {
            config.totalRequests = std::stoull(argv[++i]);
        } else if (arg == "--clients" && i + 1 < argc) {
            config.concurrency = std::stoull(argv[++i]);
        } else if (arg == "--keys" && i + 1 < argc) {
            config.keyCount = std::stoull(argv[++i]);
        } else if (arg == "--val-size" && i + 1 < argc) {
            config.valueSizeBytes = std::stoull(argv[++i]);
        } else if (arg == "--ratio" && i + 1 < argc) {
            if (!parseRatio(argv[++i], config.getWeight, config.putWeight, config.delWeight)) {
                std::cerr << "Invalid --ratio format. Expected R:W:D (e.g. 80:20:0)\n";
                return 1;
            }
        } else if (arg == "--rep" && i + 1 < argc) {
            config.replicationFactor = std::stoull(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            config.timeout = std::chrono::milliseconds(std::stoull(argv[++i]));
        } else if (arg == "--warmup" && i + 1 < argc) {
            config.warmupKeys = std::stoull(argv[++i]);
        } else if (arg == "--out" && i + 1 < argc) {
            config.csvPath = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    if (config.peers.empty()) {
        std::cerr << "Error: --peers is required.\n\n";
        printUsage(argv[0]);
        return 1;
    }

    auto peers = parsePeers(config.peers);
    if (peers.empty()) {
        std::cerr << "Error: No valid peers parsed from --peers argument.\n";
        return 1;
    }

    if (config.concurrency == 0) config.concurrency = 1;
    if (config.totalRequests == 0) config.totalRequests = 1;

    std::cout << "=================================================================\n"
              << " RingCache Benchmark Tool v" << ringcache::getVersionString() << "\n"
              << "=================================================================\n"
              << " Cluster Peers       : " << peers.size() << " node(s)\n";
    for (const auto& p : peers) {
        std::cout << "   - " << p.id << " (" << p.host << ":" << p.port << ")\n";
    }
    std::cout << " Concurrency         : " << config.concurrency << " client thread(s)\n"
              << " Total Requests      : " << config.totalRequests << "\n"
              << " Key Space Size      : " << config.keyCount << " keys\n"
              << " Value Payload Size  : " << config.valueSizeBytes << " bytes\n"
              << " Workload Mix        : " << config.getWeight << "% GET / "
                                       << config.putWeight << "% PUT / "
                                       << config.delWeight << "% DEL\n"
              << " Replication Factor  : R=" << config.replicationFactor << "\n"
              << " Timeout             : " << config.timeout.count() << " ms\n"
              << "=================================================================\n";

    std::string payload = generatePayload(config.valueSizeBytes);

    // Warm-up phase
    if (config.warmupKeys > 0 && config.getWeight > 0) {
        std::size_t warmupCount = std::min(config.warmupKeys, config.keyCount);
        std::cout << "Pre-populating " << warmupCount << " keys for warm-up...";
        std::cout.flush();

        ringcache::NetworkClusterRouter warmupRouter(
            config.replicationFactor,
            ringcache::HashRing::DEFAULT_VNODES,
            config.timeout
        );
        for (const auto& p : peers) {
            warmupRouter.addNode(p.id, p.host, p.port);
        }

        std::size_t warmed = 0;
        for (std::size_t i = 0; i < warmupCount; ++i) {
            if (warmupRouter.put("bench_key_" + std::to_string(i), payload)) {
                warmed++;
            }
        }
        std::cout << " done (" << warmed << "/" << warmupCount << " succeeded).\n";
    }

    std::cout << "\nStarting benchmark execution...\n";

    std::vector<ThreadResult> threadResults(config.concurrency);
    std::vector<std::thread> workers;
    workers.reserve(config.concurrency);

    const std::size_t reqsPerThread = config.totalRequests / config.concurrency;
    const std::size_t remainder = config.totalRequests % config.concurrency;

    auto benchStartTime = std::chrono::steady_clock::now();

    for (std::size_t i = 0; i < config.concurrency; ++i) {
        std::size_t threadOps = reqsPerThread + (i < remainder ? 1 : 0);
        workers.emplace_back(runWorker,
                             i,
                             threadOps,
                             std::cref(config),
                             std::cref(peers),
                             std::cref(payload),
                             std::ref(threadResults[i]));
    }

    for (auto& w : workers) {
        if (w.joinable()) {
            w.join();
        }
    }

    auto benchEndTime = std::chrono::steady_clock::now();
    double totalDurationSec = std::chrono::duration<double>(benchEndTime - benchStartTime).count();

    // Aggregate thread metrics
    std::size_t totalSuccess = 0;
    std::size_t totalFail = 0;
    std::size_t getSuccess = 0, getFail = 0;
    std::size_t putSuccess = 0, putFail = 0;
    std::size_t delSuccess = 0, delFail = 0;

    std::vector<uint64_t> allLatencies;
    std::vector<uint64_t> getLatencies;
    std::vector<uint64_t> putLatencies;
    std::vector<uint64_t> delLatencies;

    allLatencies.reserve(config.totalRequests);

    for (auto& tr : threadResults) {
        totalSuccess += tr.successOps;
        totalFail += tr.failedOps;
        getSuccess += tr.getSuccess;
        getFail += tr.getFail;
        putSuccess += tr.putSuccess;
        putFail += tr.putFail;
        delSuccess += tr.delSuccess;
        delFail += tr.delFail;

        allLatencies.insert(allLatencies.end(), tr.allLatenciesUs.begin(), tr.allLatenciesUs.end());
        getLatencies.insert(getLatencies.end(), tr.getLatenciesUs.begin(), tr.getLatenciesUs.end());
        putLatencies.insert(putLatencies.end(), tr.putLatenciesUs.begin(), tr.putLatenciesUs.end());
        delLatencies.insert(delLatencies.end(), tr.delLatenciesUs.begin(), tr.delLatenciesUs.end());
    }

    std::size_t totalCompletedOps = totalSuccess + totalFail;
    double throughputTotal = totalDurationSec > 0.0 ? (static_cast<double>(totalCompletedOps) / totalDurationSec) : 0.0;
    double throughputSuccess = totalDurationSec > 0.0 ? (static_cast<double>(totalSuccess) / totalDurationSec) : 0.0;

    auto allStats = computeLatencyStats(allLatencies);
    auto getStats = computeLatencyStats(getLatencies);
    auto putStats = computeLatencyStats(putLatencies);
    auto delStats = computeLatencyStats(delLatencies);

    std::cout << "\n=================================================================\n"
              << " Benchmark Results Summary\n"
              << "=================================================================\n"
              << " Total Duration      : " << std::fixed << std::setprecision(3) << totalDurationSec << " seconds\n"
              << " Total Operations    : " << totalCompletedOps << "\n"
              << "   - Successful      : " << totalSuccess << " ("
              << (totalCompletedOps > 0 ? (totalSuccess * 100.0 / totalCompletedOps) : 0.0) << "%)\n"
              << "   - Failed          : " << totalFail << " ("
              << (totalCompletedOps > 0 ? (totalFail * 100.0 / totalCompletedOps) : 0.0) << "%)\n"
              << " Throughput          : " << std::fixed << std::setprecision(1) << throughputTotal << " ops/sec (total)\n"
              << "                     : " << std::fixed << std::setprecision(1) << throughputSuccess << " ops/sec (successful)\n"
              << "-----------------------------------------------------------------\n"
              << " Latency Overview (microseconds - us):\n"
              << "   Overall:  min=" << allStats.minUs << " us, "
              << "mean=" << allStats.meanUs << " us, "
              << "p50=" << allStats.p50Us << " us, "
              << "p95=" << allStats.p95Us << " us, "
              << "p99=" << allStats.p99Us << " us, "
              << "max=" << allStats.maxUs << " us\n";

    if (!getLatencies.empty()) {
        std::cout << "   GET    :  count=" << getLatencies.size() << " (ok: " << getSuccess << ", fail: " << getFail << "), "
                  << "p50=" << getStats.p50Us << " us, p95=" << getStats.p95Us << " us, p99=" << getStats.p99Us << " us\n";
    }
    if (!putLatencies.empty()) {
        std::cout << "   PUT    :  count=" << putLatencies.size() << " (ok: " << putSuccess << ", fail: " << putFail << "), "
                  << "p50=" << putStats.p50Us << " us, p95=" << putStats.p95Us << " us, p99=" << putStats.p99Us << " us\n";
    }
    if (!delLatencies.empty()) {
        std::cout << "   DEL    :  count=" << delLatencies.size() << " (ok: " << delSuccess << ", fail: " << delFail << "), "
                  << "p50=" << delStats.p50Us << " us, p95=" << delStats.p95Us << " us, p99=" << delStats.p99Us << " us\n";
    }
    std::cout << "=================================================================\n";

    // Write CSV if requested
    if (!config.csvPath.empty()) {
        bool writeHeader = false;
        {
            std::ifstream testFile(config.csvPath);
            if (!testFile.good() || testFile.peek() == std::ifstream::traits_type::eof()) {
                writeHeader = true;
            }
        }

        std::ofstream csv(config.csvPath, std::ios::app);
        if (csv.is_open()) {
            if (writeHeader) {
                csv << "timestamp_iso,nodes,replication,concurrency,total_requests,key_count,val_size_bytes,ratio,"
                    << "duration_sec,total_ops,successful_ops,failed_ops,throughput_ops_sec,p50_us,p95_us,p99_us,min_us,mean_us,max_us\n";
            }
            std::string ratioStr = std::to_string(config.getWeight) + ":" +
                                  std::to_string(config.putWeight) + ":" +
                                  std::to_string(config.delWeight);
            csv << getIsoTimestamp() << ","
                << peers.size() << ","
                << config.replicationFactor << ","
                << config.concurrency << ","
                << config.totalRequests << ","
                << config.keyCount << ","
                << config.valueSizeBytes << ","
                << ratioStr << ","
                << std::fixed << std::setprecision(4) << totalDurationSec << ","
                << totalCompletedOps << ","
                << totalSuccess << ","
                << totalFail << ","
                << std::fixed << std::setprecision(2) << throughputTotal << ","
                << allStats.p50Us << ","
                << allStats.p95Us << ","
                << allStats.p99Us << ","
                << allStats.minUs << ","
                << allStats.meanUs << ","
                << allStats.maxUs << "\n";
            std::cout << "Results saved to CSV: " << config.csvPath << "\n";
        } else {
            std::cerr << "Warning: Failed to open CSV file for writing: " << config.csvPath << "\n";
        }
    }

    return (totalFail == 0) ? 0 : 2;
}
