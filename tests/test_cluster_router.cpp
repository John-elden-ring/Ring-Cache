#include "ringcache/cluster_router.hpp"
#include "test_common.hpp"

#include <algorithm>
#include <set>
#include <stdexcept>
#include <string>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getClusterRouterTests() {
    return {
        {
            "ClusterRoutingKeysToNodes",
            []() {
                ClusterRouter router(2);
                router.addNode("node-A", 100);
                router.addNode("node-B", 100);
                router.addNode("node-C", 100);

                if (router.nodeCount() != 3) {
                    throw std::runtime_error("Cluster should contain 3 nodes");
                }

                const std::vector<std::string> keys = {"alpha", "beta", "gamma", "delta", "epsilon"};
                for (const auto& key : keys) {
                    const auto targets = router.getTargetNodes(key);
                    if (targets.size() != 2) {
                        throw std::runtime_error("Expected 2 target nodes for key: " + key);
                    }
                    if (targets[0] == targets[1]) {
                        throw std::runtime_error("Primary and replica must be distinct nodes for key: " + key);
                    }
                    if (!router.hasNode(targets[0]) || !router.hasNode(targets[1])) {
                        throw std::runtime_error("Target nodes must be active registered nodes");
                    }
                }
            }
        },
        {
            "ClusterStoreAndRetrieve",
            []() {
                ClusterRouter router(2);
                router.addNode("srv-1", 100);
                router.addNode("srv-2", 100);
                router.addNode("srv-3", 100);

                if (!router.set("user:100", "Alice")) {
                    throw std::runtime_error("Failed to store user:100");
                }

                const auto val = router.get("user:100");
                if (!val.has_value() || *val != "Alice") {
                    throw std::runtime_error("Expected user:100 -> Alice");
                }

                const auto targets = router.getTargetNodes("user:100");
                if (targets.size() != 2) {
                    throw std::runtime_error("Expected 2 targets for user:100");
                }

                // Verify primary has the key
                if (router.peekNodeKey(targets[0], "user:100") != "Alice") {
                    throw std::runtime_error("Primary node does not contain Alice");
                }
                // Verify replica has the key
                if (router.peekNodeKey(targets[1], "user:100") != "Alice") {
                    throw std::runtime_error("Replica node does not contain Alice");
                }

                // Verify the 3rd node (non-target) does NOT have the key
                for (const auto& id : router.getNodeIds()) {
                    if (id != targets[0] && id != targets[1]) {
                        if (router.nodeContains(id, "user:100")) {
                            throw std::runtime_error("Non-target node should not contain user:100");
                        }
                    }
                }
            }
        },
        {
            "ClusterMissingKeys",
            []() {
                ClusterRouter router(2);
                router.addNode("node-1", 50);
                router.addNode("node-2", 50);

                if (router.get("non_existent").has_value()) {
                    throw std::runtime_error("get on missing key must return nullopt");
                }
                if (router.del("non_existent")) {
                    throw std::runtime_error("del on missing key must return false");
                }
            }
        },
        {
            "ClusterDeleteKeys",
            []() {
                ClusterRouter router(2);
                router.addNode("node-1", 50);
                router.addNode("node-2", 50);
                router.addNode("node-3", 50);

                router.set("temp_key", "temp_value");
                const auto targets = router.getTargetNodes("temp_key");

                if (!router.del("temp_key")) {
                    throw std::runtime_error("del on existing key must return true");
                }

                if (router.get("temp_key").has_value()) {
                    throw std::runtime_error("get after del must return nullopt");
                }

                // Verify removed from both primary and replica
                for (const auto& target : targets) {
                    if (router.nodeContains(target, "temp_key")) {
                        throw std::runtime_error("Key still present on target after del: " + target);
                    }
                }

                // Second del should return false
                if (router.del("temp_key")) {
                    throw std::runtime_error("Second del must return false");
                }
            }
        },
        {
            "ClusterReplicationAcrossDistinctNodes",
            []() {
                ClusterRouter router(3);
                router.addNode("n1", 100);
                router.addNode("n2", 100);
                router.addNode("n3", 100);
                router.addNode("n4", 100);

                const auto targets = router.getTargetNodes("replicated_key");
                if (targets.size() != 3) {
                    throw std::runtime_error("Expected exactly 3 target nodes");
                }

                std::set<std::string> uniqueTargets(targets.begin(), targets.end());
                if (uniqueTargets.size() != 3) {
                    throw std::runtime_error("Targets must contain 3 distinct physical nodes");
                }

                router.set("replicated_key", "multi_copy");

                for (const auto& target : targets) {
                    if (router.peekNodeKey(target, "replicated_key") != "multi_copy") {
                        throw std::runtime_error("Key missing on target replica: " + target);
                    }
                }
            }
        },
        {
            "ClusterReplicationFactorEdgeCases",
            []() {
                // R = 1: Single node replication
                ClusterRouter router1(1);
                router1.addNode("node-A", 100);
                router1.addNode("node-B", 100);
                const auto targets1 = router1.getTargetNodes("key1");
                if (targets1.size() != 1) {
                    throw std::runtime_error("R=1 should yield exactly 1 target");
                }
                router1.set("key1", "val1");
                if (router1.peekNodeKey(targets1[0], "key1") != "val1") {
                    throw std::runtime_error("Key1 not on primary");
                }

                // R > nodeCount(): 3 nodes, R = 5
                ClusterRouter routerBigR(5);
                routerBigR.addNode("node-1", 100);
                routerBigR.addNode("node-2", 100);
                routerBigR.addNode("node-3", 100);

                const auto targetsBigR = routerBigR.getTargetNodes("key2");
                if (targetsBigR.size() != 3) {
                    throw std::runtime_error("When R > N, should return all N distinct nodes");
                }
                std::set<std::string> uniqueBigR(targetsBigR.begin(), targetsBigR.end());
                if (uniqueBigR.size() != 3) {
                    throw std::runtime_error("Nodes must not contain duplicates when R > N");
                }

                // R = 0: Replication disabled
                ClusterRouter routerZeroR(0);
                routerZeroR.addNode("node-1", 100);
                if (routerZeroR.set("key3", "val3")) {
                    throw std::runtime_error("set should return false when R = 0");
                }
                if (routerZeroR.get("key3").has_value()) {
                    throw std::runtime_error("get should return nullopt when R = 0");
                }
                if (!routerZeroR.getTargetNodes("key3").empty()) {
                    throw std::runtime_error("getTargetNodes must be empty when R = 0");
                }
            }
        },
        {
            "ClusterAddAndRemoveNodes",
            []() {
                ClusterRouter router(2);
                router.addNode("srv-A", 100);
                router.addNode("srv-B", 100);
                router.addNode("srv-C", 100);

                for (int i = 0; i < 20; ++i) {
                    router.set("initial_key_" + std::to_string(i), "data_" + std::to_string(i));
                }

                // Add 4th node
                if (!router.addNode("srv-D", 100)) {
                    throw std::runtime_error("Failed to add srv-D");
                }
                if (router.nodeCount() != 4) {
                    throw std::runtime_error("Node count should be 4");
                }

                // Duplicate addNode should fail
                if (router.addNode("srv-D", 100)) {
                    throw std::runtime_error("Duplicate addNode must return false");
                }

                // Remove srv-B
                if (!router.removeNode("srv-B")) {
                    throw std::runtime_error("Failed to remove srv-B");
                }
                if (router.hasNode("srv-B")) {
                    throw std::runtime_error("srv-B should be gone");
                }
                if (router.nodeCount() != 3) {
                    throw std::runtime_error("Node count should be 3 after removal");
                }

                // Removing non-existent node
                if (router.removeNode("srv-B")) {
                    throw std::runtime_error("Removing already removed node should return false");
                }
            }
        },
        {
            "ClusterPrimaryAbsentFallbackToReplica",
            []() {
                ClusterRouter router(2);
                router.addNode("primary-candidate", 100);
                router.addNode("replica-candidate", 100);

                const std::string testKey = "resilient_data";
                const std::string testVal = "persists_on_replica";

                router.set(testKey, testVal);

                const auto targets = router.getTargetNodes(testKey);
                if (targets.size() != 2) {
                    throw std::runtime_error("Expected 2 targets");
                }

                // Directly clear the primary node to simulate primary node failure / local loss
                // without modifying the routing topology
                const std::string primaryId = targets[0];
                const std::string replicaId = targets[1];

                // Verify replica has the key
                if (router.peekNodeKey(replicaId, testKey) != testVal) {
                    throw std::runtime_error("Replica does not have the value");
                }

                // Remove key directly from primary only
                // Router's del() removes from all, so we use clearAllNodes then re-seed replica,
                // or remove node. Let's test removal of primary from cluster:
                router.removeNode(primaryId);

                // With primary removed, replica becomes the new primary on the ring!
                // Or if we check read:
                const auto fallbackVal = router.get(testKey);
                if (!fallbackVal.has_value() || *fallbackVal != testVal) {
                    throw std::runtime_error("Read should succeed from replica after primary removed");
                }
            }
        },
        {
            "ClusterLruEvictionAndReplicas",
            []() {
                // Nodes with capacity 2
                ClusterRouter router(2);
                router.addNode("box-1", 2);
                router.addNode("box-2", 2);
                router.addNode("box-3", 2);

                // Insert 10 keys across the 3 nodes
                for (int i = 0; i < 10; ++i) {
                    router.set("k_" + std::to_string(i), "v_" + std::to_string(i));
                }

                // Each node must respect its capacity limit of 2
                for (const auto& id : router.getNodeIds()) {
                    const auto size = router.getNodeSize(id);
                    if (!size.has_value() || *size > 2) {
                        throw std::runtime_error("Node " + id + " exceeded capacity 2");
                    }
                }
            }
        },
        {
            "ClusterPartialWriteFailureAndConsistency",
            []() {
                ClusterRouter router(2);
                router.addNode("capable-node", 50);
                router.addNode("zero-cap-node", 0); // Capacity 0 cannot store anything

                // Case 1: capable-node is primary -> write succeeds on primary, fails on zero-cap replica
                // We test multiple keys to find one where capable-node is primary
                bool foundPrimary = false;
                for (int i = 0; i < 50; ++i) {
                    const std::string k = "probe_key_" + std::to_string(i);
                    const auto targets = router.getTargetNodes(k);
                    if (targets.size() == 2 && targets[0] == "capable-node") {
                        const WriteResult res = router.setWithDetails(k, "val");
                        if (!res.success) {
                            throw std::runtime_error("Write should succeed when primary succeeds");
                        }
                        if (res.writtenCount != 1 || res.targetCount != 2) {
                            throw std::runtime_error("Expected partial write (1 of 2 targets written)");
                        }
                        if (router.get(k) != "val") {
                            throw std::runtime_error("Value should be retrievable from primary");
                        }
                        foundPrimary = true;
                        break;
                    }
                }

                if (!foundPrimary) {
                    throw std::runtime_error("Could not find key with capable-node as primary");
                }

                // Case 2: zero-cap-node is primary -> write must fail
                bool foundZeroPrimary = false;
                for (int i = 0; i < 50; ++i) {
                    const std::string k = "probe_zero_" + std::to_string(i);
                    const auto targets = router.getTargetNodes(k);
                    if (targets.size() == 2 && targets[0] == "zero-cap-node") {
                        const WriteResult res = router.setWithDetails(k, "val");
                        if (res.success) {
                            throw std::runtime_error("Write must fail when primary fails");
                        }
                        if (res.writtenCount != 0) {
                            throw std::runtime_error("Written count must be 0 when primary fails");
                        }
                        foundZeroPrimary = true;
                        break;
                    }
                }

                if (!foundZeroPrimary) {
                    throw std::runtime_error("Could not find key with zero-cap-node as primary");
                }
            }
        }
    };
}

} // namespace ringcache::test
