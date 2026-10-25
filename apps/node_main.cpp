#include "ringcache/cache_node.hpp"
#include "ringcache/tcp_server.hpp"
#include "ringcache/version.hpp"

#include <csignal>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <string_view>
#include <vector>

namespace {

std::atomic<ringcache::TcpServer*> gServerInstance{nullptr};

void signalHandler(int sig) {
    std::cout << "\nCaught signal " << sig << ", initiating graceful shutdown...\n";
    auto* server = gServerInstance.load();
    if (server) {
        server->stop();
    }
}

void printUsage(std::string_view progName) {
    std::cout << "RingCache Node Server v" << ringcache::getVersionString() << "\n"
              << "Usage: " << progName << " [options]\n\n"
              << "Options:\n"
              << "  --id <string>         Node identifier (default: node-1)\n"
              << "  --host <ip>           Listen address (default: 127.0.0.1)\n"
              << "  --port <port>         Listen port (default: 8001)\n"
              << "  --capacity <size>     LRU cache capacity (default: 1000)\n"
              << "  --threads <count>     Worker threads (default: 2)\n"
              << "  --peers <list>        Static cluster membership (e.g. n1=127.0.0.1:8001,n2=127.0.0.1:8002)\n"
              << "  --help                Show this help message\n";
}

} // namespace

int main(int argc, char* argv[]) {
    std::string nodeId = "node-1";
    std::string host = "127.0.0.1";
    uint16_t port = 8001;
    std::size_t capacity = 1000;
    std::size_t threads = 2;
    std::string peers;

    for (int i = 1; i < argc; ++i) {
        std::string_view arg = argv[i];
        if (arg == "--id" && i + 1 < argc) {
            nodeId = argv[++i];
        } else if (arg == "--host" && i + 1 < argc) {
            host = argv[++i];
        } else if (arg == "--port" && i + 1 < argc) {
            port = static_cast<uint16_t>(std::stoul(argv[++i]));
        } else if (arg == "--capacity" && i + 1 < argc) {
            capacity = std::stoull(argv[++i]);
        } else if (arg == "--threads" && i + 1 < argc) {
            threads = std::stoull(argv[++i]);
        } else if (arg == "--peers" && i + 1 < argc) {
            peers = argv[++i];
        } else if (arg == "--help" || arg == "-h") {
            printUsage(argv[0]);
            return 0;
        } else {
            std::cerr << "Unknown or incomplete argument: " << arg << "\n";
            printUsage(argv[0]);
            return 1;
        }
    }

    std::cout << "Starting RingCache Node: " << nodeId
              << " on " << host << ":" << port
              << " [capacity: " << capacity << ", threads: " << threads << "]\n";
    if (!peers.empty()) {
        std::cout << "Configured cluster peers: " << peers << "\n";
    }

    auto cacheNode = std::make_shared<ringcache::CacheNode>(nodeId, capacity);

    ringcache::ServerConfig config;
    config.host = host;
    config.port = port;
    config.numThreads = threads;

    ringcache::TcpServer server(cacheNode, config);
    gServerInstance.store(&server);

    std::signal(SIGINT, signalHandler);
    std::signal(SIGTERM, signalHandler);

    try {
        server.run();
    } catch (const std::exception& ex) {
        std::cerr << "Server error: " << ex.what() << "\n";
        return 1;
    }

    gServerInstance.store(nullptr);
    std::cout << "RingCache Node " << nodeId << " stopped cleanly.\n";
    return 0;
}
