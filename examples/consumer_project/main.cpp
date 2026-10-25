#include <ringcache/client.hpp>
#include <ringcache/version.hpp>

#include <iostream>
#include <string>

int main(int argc, char* argv[]) {
    std::cout << "========================================\n"
              << " RingCache Example Consumer Application \n"
              << " Using RingCache Library v" << ringcache::getVersionString() << "\n"
              << "========================================\n";

    std::string peers = "node-1=127.0.0.1:18881";
    if (argc > 1) {
        peers = argv[1];
    }

    std::cout << "Connecting to cluster peers: " << peers << " ...\n";

    try {
        ringcache::ClientConfig config;
        config.peers = peers;
        config.replicationFactor = 1;
        config.timeout = std::chrono::milliseconds(1500);

        ringcache::Client client(config);
        std::cout << "Initialized RingCache client with " << client.nodeCount()
                  << " node(s), replication factor R=" << client.getReplicationFactor() << "\n";

        // 1. Health check
        ringcache::ClientError err;
        if (client.pingAll(&err)) {
            std::cout << "[OK] Cluster ping successful.\n";
        } else {
            std::cout << "[INFO] Cluster ping probe returned: " << err.message
                      << " (continuing demonstration)\n";
        }

        // 2. PUT operation
        std::string testKey = "greeting";
        std::string testVal = "Hello from independent consumer project!";

        std::cout << "Storing key '" << testKey << "' ...\n";
        if (client.put(testKey, testVal, &err)) {
            std::cout << "[OK] Stored key successfully.\n";
        } else {
            std::cerr << "[FAIL] Failed to store key: " << err.message << " (code: "
                      << static_cast<int>(err.code) << ")\n";
        }

        // 3. GET operation
        std::cout << "Retrieving key '" << testKey << "' ...\n";
        auto retrieved = client.get(testKey, &err);
        if (retrieved.has_value()) {
            std::cout << "[OK] Retrieved value: \"" << *retrieved << "\"\n";
        } else {
            std::cerr << "[FAIL] Could not retrieve key: " << err.message << "\n";
        }

        // 4. PUT with metrics
        auto metrics = client.putWithMetrics("stats_key", "sample_metric", &err);
        std::cout << "Stored 'stats_key' across " << metrics.writtenCount << " node(s). Primary: "
                  << metrics.primaryNode << "\n";

        // 5. DELETE operation
        if (client.del(testKey, &err)) {
            std::cout << "[OK] Deleted key '" << testKey << "' successfully.\n";
        } else {
            std::cerr << "[INFO] DEL returned: " << err.message << "\n";
        }

        std::cout << "========================================\n"
                  << " Example completed successfully.\n"
                  << "========================================\n";
        return 0;

    } catch (const std::exception& ex) {
        std::cerr << "Consumer error: " << ex.what() << "\n";
        return 1;
    }
}
