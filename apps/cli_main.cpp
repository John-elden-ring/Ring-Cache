#include "ringcache/network_cluster_router.hpp"
#include "ringcache/version.hpp"

#include <iostream>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

namespace {

void printUsage(std::string_view progName) {
    std::cout << "RingCache CLI Client v" << ringcache::getVersionString() << "\n"
              << "Usage: " << progName << " --peers <node1=ip:port,...> [options] <command> [args...]\n\n"
              << "Options:\n"
              << "  --peers <list>     Comma-separated node endpoints (e.g. n1=127.0.0.1:8001,n2=127.0.0.1:8002)\n"
              << "  --rep <factor>     Replication factor R (default: 1)\n"
              << "  --timeout <ms>     Operation timeout in milliseconds (default: 2000)\n\n"
              << "Commands:\n"
              << "  put <key> <value>  Store key-value pair across primary and replicas\n"
              << "  get <key>          Retrieve value with primary-first and replica fallback\n"
              << "  del <key>          Delete key from primary and replicas\n"
              << "  ping <nodeId>      Ping a specific node\n";
}

bool parsePeer(const std::string& peerStr, std::string& nodeId, std::string& host, uint16_t& port) {
    auto eqPos = peerStr.find('=');
    if (eqPos == std::string::npos) return false;
    nodeId = peerStr.substr(0, eqPos);

    auto colonPos = peerStr.find(':', eqPos + 1);
    if (colonPos == std::string::npos) return false;
    host = peerStr.substr(eqPos + 1, colonPos - (eqPos + 1));
    port = static_cast<uint16_t>(std::stoul(peerStr.substr(colonPos + 1)));
    return true;
}

} // namespace

int main(int argc, char* argv[]) {
    if (argc < 2) {
        printUsage(argv[0]);
        return 1;
    }

    std::string peersArg;
    std::size_t replicationFactor = 1;
    std::chrono::milliseconds timeout{2000};
    int cmdIdx = -1;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--peers" && i + 1 < argc) {
            peersArg = argv[++i];
        } else if (arg == "--rep" && i + 1 < argc) {
            replicationFactor = std::stoull(argv[++i]);
        } else if (arg == "--timeout" && i + 1 < argc) {
            timeout = std::chrono::milliseconds(std::stoull(argv[++i]));
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else if (arg[0] != '-') {
            cmdIdx = i;
            break;
        }
    }

    if (peersArg.empty()) {
        std::cerr << "Error: --peers argument is required.\n";
        printUsage(argv[0]);
        return 1;
    }

    if (cmdIdx == -1 || cmdIdx >= argc) {
        std::cerr << "Error: No command specified.\n";
        printUsage(argv[0]);
        return 1;
    }

    ringcache::NetworkClusterRouter router(replicationFactor, ringcache::HashRing::DEFAULT_VNODES, timeout);

    std::stringstream ss(peersArg);
    std::string item;
    while (std::getline(ss, item, ',')) {
        if (item.empty()) continue;
        std::string nid, host;
        uint16_t port;
        if (!parsePeer(item, nid, host, port)) {
            std::cerr << "Error parsing peer: " << item << " (expected format: id=host:port)\n";
            return 1;
        }
        if (!router.addNode(nid, host, port)) {
            std::cerr << "Warning: Failed to add node " << nid << "\n";
        }
    }

    std::string command = argv[cmdIdx];

    if (command == "put") {
        if (cmdIdx + 2 >= argc) {
            std::cerr << "Usage: put <key> <value>\n";
            return 1;
        }
        std::string key = argv[cmdIdx + 1];
        std::string value = argv[cmdIdx + 2];
        auto details = router.putWithDetails(key, value);
        if (details.success) {
            std::cout << "OK (written to primary " << details.primaryNode
                      << ", total copies: " << details.writtenCount << "/" << details.targetCount << ")\n";
            return 0;
        } else {
            std::cout << "FAILED (target: " << details.primaryNode << ")\n";
            return 1;
        }
    } else if (command == "get") {
        if (cmdIdx + 1 >= argc) {
            std::cerr << "Usage: get <key>\n";
            return 1;
        }
        std::string key = argv[cmdIdx + 1];
        auto val = router.get(key);
        if (val.has_value()) {
            std::cout << *val << "\n";
            return 0;
        } else {
            std::cout << "(nil)\n";
            return 1;
        }
    } else if (command == "del") {
        if (cmdIdx + 1 >= argc) {
            std::cerr << "Usage: del <key>\n";
            return 1;
        }
        std::string key = argv[cmdIdx + 1];
        bool ok = router.del(key);
        std::cout << (ok ? "1" : "0") << "\n";
        return ok ? 0 : 1;
    } else if (command == "ping") {
        if (cmdIdx + 1 >= argc) {
            std::cerr << "Usage: ping <nodeId>\n";
            return 1;
        }
        std::string nid = argv[cmdIdx + 1];
        bool ok = router.pingNode(nid);
        std::cout << (ok ? "PONG\n" : "NO_RESPONSE\n");
        return ok ? 0 : 1;
    } else {
        std::cerr << "Unknown command: " << command << "\n";
        printUsage(argv[0]);
        return 1;
    }
}
