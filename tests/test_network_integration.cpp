#include "ringcache/network_client.hpp"
#include "ringcache/network_cluster_router.hpp"
#include "ringcache/tcp_server.hpp"
#include "test_common.hpp"
#include "test_process_helpers.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getNetworkIntegrationTests() {
    return {
        {"Network: In-Process Client-Server Round Trip", []() {
            auto node = std::make_shared<CacheNode>("node-local", 50);
            ServerConfig config;
            config.host = "127.0.0.1";
            config.port = 0; // OS ephemeral port
            config.numThreads = 2;

            TcpServer server(node, config);
            server.start();

            uint16_t port = server.getBoundPort();
            RC_TEST_ASSERT(port > 0, "Server should bind to valid port");

            NetworkClient client("127.0.0.1", port, std::chrono::milliseconds(1000));
            RC_TEST_ASSERT(client.ping(), "Client should ping local server successfully");

            // PUT
            RC_TEST_ASSERT(client.put("color", "blue"), "Client PUT should succeed");

            // GET
            auto val = client.get("color");
            RC_TEST_ASSERT(val.has_value(), "Client GET should return value");
            RC_TEST_ASSERT(*val == "blue", "Client GET value should match 'blue'");

            // DELETE
            RC_TEST_ASSERT(client.del("color"), "Client DEL should succeed");
            RC_TEST_ASSERT(!client.get("color").has_value(), "Key should be gone after DEL");

            server.stop();
        }},

        {"Network: Multi-Process Cluster Operations and Replication", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist for integration tests");

            const uint16_t port1 = 18701;
            const uint16_t port2 = 18702;
            const uint16_t port3 = 18703;

            std::string cmd1 = "\"" + binary + "\" --id node-1 --port " + std::to_string(port1) + " --capacity 500";
            std::string cmd2 = "\"" + binary + "\" --id node-2 --port " + std::to_string(port2) + " --capacity 500";
            std::string cmd3 = "\"" + binary + "\" --id node-3 --port " + std::to_string(port3) + " --capacity 500";

            ChildProcess proc1, proc2, proc3;
            RC_TEST_ASSERT(proc1.start(cmd1), "Failed to launch node-1 process");
            RC_TEST_ASSERT(proc2.start(cmd2), "Failed to launch node-2 process");
            RC_TEST_ASSERT(proc3.start(cmd3), "Failed to launch node-3 process");

            RC_TEST_ASSERT(waitForNodeReady(port1), "Node 1 did not become ready");
            RC_TEST_ASSERT(waitForNodeReady(port2), "Node 2 did not become ready");
            RC_TEST_ASSERT(waitForNodeReady(port3), "Node 3 did not become ready");

            // Create cluster router with R=2
            NetworkClusterRouter router(2, 100, std::chrono::milliseconds(1000));
            RC_TEST_ASSERT(router.addNode("node-1", "127.0.0.1", port1), "Add node-1");
            RC_TEST_ASSERT(router.addNode("node-2", "127.0.0.1", port2), "Add node-2");
            RC_TEST_ASSERT(router.addNode("node-3", "127.0.0.1", port3), "Add node-3");

            RC_TEST_ASSERT(router.nodeCount() == 3, "Router should have 3 nodes");

            // Test PUT and replication across distinct nodes
            const std::vector<std::pair<std::string, std::string>> kvPairs = {
                {"user:1", "Alice"},
                {"user:2", "Bob"},
                {"user:3", "Charlie"},
                {"user:4", "Diana"}
            };

            for (const auto& [k, v] : kvPairs) {
                auto details = router.putWithDetails(k, v);
                RC_TEST_ASSERT(details.success, "Write should succeed for " + k);
                RC_TEST_ASSERT(details.writtenCount == 2, "Should replicate to primary + 1 replica (R=2)");
                RC_TEST_ASSERT(!details.primaryNode.empty(), "Primary node recorded");
                RC_TEST_ASSERT(details.replicaNodes.size() == 1, "One replica recorded");
            }

            // Test GET
            for (const auto& [k, v] : kvPairs) {
                auto val = router.get(k);
                RC_TEST_ASSERT(val.has_value() && *val == v, "Value retrieved must match for " + k);
            }

            // Test DEL
            RC_TEST_ASSERT(router.del("user:1"), "Delete user:1 should succeed");
            RC_TEST_ASSERT(!router.get("user:1").has_value(), "user:1 should be gone after delete");

            // Verify clean shutdown
            proc1.stop();
            proc2.stop();
            proc3.stop();
        }},

        {"Network: Replica Fallback When Primary Node Process Drops", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist");

            const uint16_t port1 = 18711;
            const uint16_t port2 = 18712;
            const uint16_t port3 = 18713;

            std::string cmd1 = "\"" + binary + "\" --id node-A --port " + std::to_string(port1) + " --capacity 500";
            std::string cmd2 = "\"" + binary + "\" --id node-B --port " + std::to_string(port2) + " --capacity 500";
            std::string cmd3 = "\"" + binary + "\" --id node-C --port " + std::to_string(port3) + " --capacity 500";

            auto p1 = std::make_unique<ChildProcess>();
            auto p2 = std::make_unique<ChildProcess>();
            auto p3 = std::make_unique<ChildProcess>();

            RC_TEST_ASSERT(p1->start(cmd1), "Start node-A");
            RC_TEST_ASSERT(p2->start(cmd2), "Start node-B");
            RC_TEST_ASSERT(p3->start(cmd3), "Start node-C");

            RC_TEST_ASSERT(waitForNodeReady(port1), "Wait node-A");
            RC_TEST_ASSERT(waitForNodeReady(port2), "Wait node-B");
            RC_TEST_ASSERT(waitForNodeReady(port3), "Wait node-C");

            NetworkClusterRouter router(2, 100, std::chrono::milliseconds(500));
            router.addNode("node-A", "127.0.0.1", port1);
            router.addNode("node-B", "127.0.0.1", port2);
            router.addNode("node-C", "127.0.0.1", port3);

            std::string testKey = "resilience_key";
            std::string testVal = "durable_data";

            auto details = router.putWithDetails(testKey, testVal);
            RC_TEST_ASSERT(details.success, "Initial write must succeed");
            RC_TEST_ASSERT(details.writtenCount == 2, "Initial write must replicate to 2 nodes");

            std::string primaryId = details.primaryNode;

            // Kill the primary process!
            if (primaryId == "node-A") {
                p1->stop();
            } else if (primaryId == "node-B") {
                p2->stop();
            } else {
                p3->stop();
            }

            // Primary is dead! Now perform GET on testKey.
            // Router must failover to the replica over TCP without crashing.
            auto retrievedVal = router.get(testKey);
            RC_TEST_ASSERT(retrievedVal.has_value(), "GET should succeed via replica fallback after primary killed");
            RC_TEST_ASSERT(*retrievedVal == testVal, "Retrieved value from replica must match original value");

            p1->stop();
            p2->stop();
            p3->stop();
        }},

        {"Network: Error Handling, Malformed Payloads, and Timeouts", []() {
            auto node = std::make_shared<CacheNode>("node-err", 50);
            ServerConfig config;
            config.host = "127.0.0.1";
            config.port = 0;

            TcpServer server(node, config);
            server.start();

            uint16_t port = server.getBoundPort();

            // 1. Raw socket malformed frame
            asio::io_context io;
            asio::ip::tcp::socket rawSocket(io);
            rawSocket.connect(asio::ip::tcp::endpoint(asio::ip::make_address("127.0.0.1"), port));

            std::string badFrame = "{\"bad_json\": [not closed\n";
            asio::write(rawSocket, asio::buffer(badFrame));

            asio::streambuf buf;
            asio::read_until(rawSocket, buf, '\n');
            std::istream is(&buf);
            std::string resLine;
            std::getline(is, resLine);

            auto res = parseResponse(resLine);
            RC_TEST_ASSERT(res.has_value(), "Server must respond to malformed request");
            RC_TEST_ASSERT(res->status == StatusCode::Malformed, "Response status must be MALFORMED");

            // Server must still be alive and responsive!
            NetworkClient client("127.0.0.1", port, std::chrono::milliseconds(500));
            RC_TEST_ASSERT(client.ping(), "Server should still be responsive after malformed payload");

            // 2. Unreachable node timeout
            NetworkClient deadClient("127.0.0.1", 19999, std::chrono::milliseconds(200));
            std::string errMsg;
            RC_TEST_ASSERT(!deadClient.ping(&errMsg), "Ping to dead port should fail");
            RC_TEST_ASSERT(!errMsg.empty(), "Error message should describe connection failure");

            server.stop();
        }}
    };
}

} // namespace ringcache::test
