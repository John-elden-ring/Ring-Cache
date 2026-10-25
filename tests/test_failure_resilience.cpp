#include "ringcache/network_client.hpp"
#include "ringcache/network_cluster_router.hpp"
#include "test_common.hpp"
#include "test_process_helpers.hpp"

#include <chrono>
#include <iostream>
#include <memory>
#include <string>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getFailureResilienceTests() {
    return {
        {"Failure: Primary Node Termination During Read Workload (Replica Fallback)", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist");

            const uint16_t port1 = 18721;
            const uint16_t port2 = 18722;
            const uint16_t port3 = 18723;

            std::string cmd1 = "\"" + binary + "\" --id node-1 --port " + std::to_string(port1) + " --capacity 500";
            std::string cmd2 = "\"" + binary + "\" --id node-2 --port " + std::to_string(port2) + " --capacity 500";
            std::string cmd3 = "\"" + binary + "\" --id node-3 --port " + std::to_string(port3) + " --capacity 500";

            auto p1 = std::make_unique<ChildProcess>();
            auto p2 = std::make_unique<ChildProcess>();
            auto p3 = std::make_unique<ChildProcess>();

            RC_TEST_ASSERT(p1->start(cmd1), "Start node-1");
            RC_TEST_ASSERT(p2->start(cmd2), "Start node-2");
            RC_TEST_ASSERT(p3->start(cmd3), "Start node-3");

            RC_TEST_ASSERT(waitForNodeReady(port1), "Wait node-1 ready");
            RC_TEST_ASSERT(waitForNodeReady(port2), "Wait node-2 ready");
            RC_TEST_ASSERT(waitForNodeReady(port3), "Wait node-3 ready");

            NetworkClusterRouter router(2, 100, std::chrono::milliseconds(500));
            router.addNode("node-1", "127.0.0.1", port1);
            router.addNode("node-2", "127.0.0.1", port2);
            router.addNode("node-3", "127.0.0.1", port3);

            // Populate multiple keys and record their primary nodes
            std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon", "zeta", "eta", "theta"};
            std::string killedNodeId = "node-2";
            std::vector<std::string> keysWithKilledPrimary;

            for (const auto& k : keys) {
                std::string val = "val_" + k;
                auto details = router.putWithDetails(k, val);
                RC_TEST_ASSERT(details.success, "PUT must succeed for " + k);
                RC_TEST_ASSERT(details.writtenCount == 2, "Replication factor 2 must be satisfied");

                if (details.primaryNode == killedNodeId) {
                    keysWithKilledPrimary.push_back(k);
                }
            }

            // Abruptly terminate node-2 mid-cluster lifecycle
            p2->stop();

            // Verify that keys whose primary was killed are successfully served by their replica
            for (const auto& k : keysWithKilledPrimary) {
                auto val = router.get(k);
                RC_TEST_ASSERT(val.has_value(), "Read on " + k + " must fall back to replica when primary is dead");
                RC_TEST_ASSERT(*val == "val_" + k, "Retrieved value from replica must match original content");
            }

            // Verify that keys whose primary was node-1 or node-3 still read normally
            for (const auto& k : keys) {
                if (std::find(keysWithKilledPrimary.begin(), keysWithKilledPrimary.end(), k) == keysWithKilledPrimary.end()) {
                    auto val = router.get(k);
                    RC_TEST_ASSERT(val.has_value(), "Read on " + k + " should succeed via surviving primary");
                    RC_TEST_ASSERT(*val == "val_" + k, "Content must match");
                }
            }

            p1->stop();
            p3->stop();
        }},

        {"Failure: Primary Node Termination During Writes (Phase 1 Limitation)", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist");

            const uint16_t port1 = 18731;
            const uint16_t port2 = 18732;
            const uint16_t port3 = 18733;

            std::string cmd1 = "\"" + binary + "\" --id node-1 --port " + std::to_string(port1) + " --capacity 500";
            std::string cmd2 = "\"" + binary + "\" --id node-2 --port " + std::to_string(port2) + " --capacity 500";
            std::string cmd3 = "\"" + binary + "\" --id node-3 --port " + std::to_string(port3) + " --capacity 500";

            auto p1 = std::make_unique<ChildProcess>();
            auto p2 = std::make_unique<ChildProcess>();
            auto p3 = std::make_unique<ChildProcess>();

            RC_TEST_ASSERT(p1->start(cmd1), "Start node-1");
            RC_TEST_ASSERT(p2->start(cmd2), "Start node-2");
            RC_TEST_ASSERT(p3->start(cmd3), "Start node-3");

            RC_TEST_ASSERT(waitForNodeReady(port1), "Wait node-1");
            RC_TEST_ASSERT(waitForNodeReady(port2), "Wait node-2");
            RC_TEST_ASSERT(waitForNodeReady(port3), "Wait node-3");

            NetworkClusterRouter router(2, 100, std::chrono::milliseconds(300));
            router.addNode("node-1", "127.0.0.1", port1);
            router.addNode("node-2", "127.0.0.1", port2);
            router.addNode("node-3", "127.0.0.1", port3);

            // Find a key whose primary is node-1, and one whose primary is node-2
            std::string keyOnNode1;
            std::string keyOnNode2;

            for (int i = 0; i < 500; ++i) {
                std::string candidate = "candidate_key_" + std::to_string(i);
                auto targets = router.getTargetNodes(candidate);
                if (!targets.empty()) {
                    if (targets.front() == "node-1" && keyOnNode1.empty()) {
                        keyOnNode1 = candidate;
                    } else if (targets.front() == "node-2" && keyOnNode2.empty()) {
                        keyOnNode2 = candidate;
                    }
                }
                if (!keyOnNode1.empty() && !keyOnNode2.empty()) {
                    break;
                }
            }

            RC_TEST_ASSERT(!keyOnNode1.empty(), "Found key routed to node-1 primary");
            RC_TEST_ASSERT(!keyOnNode2.empty(), "Found key routed to node-2 primary");

            // Kill node-1
            p1->stop();

            // Attempt write to keyOnNode1 (primary is down)
            // Phase 1 architectural invariant: writes to a dead primary fail because
            // static ring routing does not perform dynamic re-balancing or promotion without reconfiguration.
            auto writeResult1 = router.putWithDetails(keyOnNode1, "payload-1");
            RC_TEST_ASSERT(!writeResult1.success, "Write must fail when primary node is unreachable in Phase 1 static ring");
            RC_TEST_ASSERT(writeResult1.writtenCount == 0, "No replicas written if primary write failed");

            // Attempt write to keyOnNode2 (primary node-2 is alive; replica is node-3 or node-1)
            auto writeResult2 = router.putWithDetails(keyOnNode2, "payload-2");
            RC_TEST_ASSERT(writeResult2.success, "Write to surviving primary node must succeed");
            RC_TEST_ASSERT(writeResult2.writtenCount >= 1, "At least primary must be written");

            // Value on surviving primary should be readable
            auto val2 = router.get(keyOnNode2);
            RC_TEST_ASSERT(val2.has_value() && *val2 == "payload-2", "Value on surviving primary must be retrievable");

            p2->stop();
            p3->stop();
        }},

        {"Failure: Node Process Restart and Automatic Client Reconnection", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist");

            const uint16_t port = 18741;
            std::string cmd = "\"" + binary + "\" --id node-restart --port " + std::to_string(port) + " --capacity 200";

            auto proc = std::make_unique<ChildProcess>();
            RC_TEST_ASSERT(proc->start(cmd), "Start restartable node");
            RC_TEST_ASSERT(waitForNodeReady(port), "Wait for node ready");

            NetworkClient client("127.0.0.1", port, std::chrono::milliseconds(500));
            RC_TEST_ASSERT(client.ping(), "Initial ping should succeed");
            RC_TEST_ASSERT(client.put("session_key", "session_data"), "PUT should succeed");
            RC_TEST_ASSERT(client.get("session_key") == "session_data", "GET should succeed");

            // Stop the node process
            proc->stop();

            // Attempt operation on dead node: must fail cleanly and report error
            std::string errMsg;
            RC_TEST_ASSERT(!client.ping(&errMsg), "Ping on stopped node must fail");
            RC_TEST_ASSERT(!errMsg.empty(), "Error message must be populated on failure");

            // Restart node process on the exact same port
            auto proc2 = std::make_unique<ChildProcess>();
            RC_TEST_ASSERT(proc2->start(cmd), "Restart node process on same port");
            RC_TEST_ASSERT(waitForNodeReady(port), "Wait for restarted node to be ready");

            // Client must automatically reconnect on the next request without crashing
            RC_TEST_ASSERT(client.ping(), "Client must reconnect cleanly after node restart");

            // Verify fresh write to restarted node succeeds
            RC_TEST_ASSERT(client.put("new_key", "fresh_data"), "PUT after reconnect must succeed");
            RC_TEST_ASSERT(client.get("new_key") == "fresh_data", "GET after reconnect must succeed");

            proc2->stop();
        }},

        {"Failure: Unreachable Host Timeout Bound", []() {
            // Pick a port that is not listening
            const uint16_t deadPort = 19998;
            auto start = std::chrono::steady_clock::now();

            NetworkClient deadClient("127.0.0.1", deadPort, std::chrono::milliseconds(250));
            std::string errMsg;
            bool ok = deadClient.ping(&errMsg);
            auto elapsed = std::chrono::duration_cast<std::chrono::milliseconds>(
                std::chrono::steady_clock::now() - start
            );

            RC_TEST_ASSERT(!ok, "Ping to dead port must fail");
            RC_TEST_ASSERT(!errMsg.empty(), "Error message should describe connection failure");
            // Must return within reasonable bound (timeout was 250ms, allow margin for OS error response)
            RC_TEST_ASSERT(elapsed.count() < 3000, "Timeout must abort cleanly without stalling indefinitely");
        }}
    };
}

} // namespace ringcache::test
