#include "ringcache/hash_ring.hpp"
#include "test_common.hpp"

#include <cmath>
#include <map>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getHashRingTests() {
    return {
        {
            "HashRingEmptyRing",
            []() {
                HashRing ring;
                if (!ring.empty()) {
                    throw std::runtime_error("New ring must be empty");
                }
                if (ring.nodeCount() != 0) {
                    throw std::runtime_error("Empty ring nodeCount must be 0");
                }
                if (ring.ringSize() != 0) {
                    throw std::runtime_error("Empty ring ringSize must be 0");
                }
                if (ring.getNode("test_key").has_value()) {
                    throw std::runtime_error("Empty ring getNode must return nullopt");
                }
                if (ring.getNode("").has_value()) {
                    throw std::runtime_error("getNode on empty key must return nullopt");
                }
                if (!ring.getNodes().empty()) {
                    throw std::runtime_error("getNodes on empty ring must return empty vector");
                }
                if (ring.removeNode("node-1")) {
                    throw std::runtime_error("removeNode on empty ring must return false");
                }
            }
        },
        {
            "HashRingAddNodeAndLookup",
            []() {
                HashRing ring;
                if (!ring.addNode("node-1", 100)) {
                    throw std::runtime_error("Failed to add node-1");
                }
                if (ring.empty()) {
                    throw std::runtime_error("Ring should not be empty after adding a node");
                }
                if (ring.nodeCount() != 1) {
                    throw std::runtime_error("Node count should be 1");
                }
                if (ring.ringSize() != 100) {
                    throw std::runtime_error("Ring size should equal 100");
                }
                if (!ring.hasNode("node-1")) {
                    throw std::runtime_error("hasNode('node-1') must be true");
                }
                if (ring.hasNode("node-2")) {
                    throw std::runtime_error("hasNode('node-2') must be false");
                }

                const std::vector<std::string> keys = {
                    "user:101", "session:abc", "page:home", "metric:cpu", "item:9999"
                };
                for (const auto& key : keys) {
                    const auto node = ring.getNode(key);
                    if (!node.has_value() || *node != "node-1") {
                        throw std::runtime_error("All keys must route to the only node ('node-1')");
                    }
                }
            }
        },
        {
            "HashRingDeterministicLookup",
            []() {
                HashRing ring;
                ring.addNode("node-A", 150);
                ring.addNode("node-B", 150);
                ring.addNode("node-C", 150);

                std::vector<std::pair<std::string, std::string>> keyToNode;
                keyToNode.reserve(200);

                for (int i = 0; i < 200; ++i) {
                    const std::string key = "deterministic_key_" + std::to_string(i);
                    const auto node = ring.getNode(key);
                    if (!node.has_value()) {
                        throw std::runtime_error("Node lookup failed for: " + key);
                    }
                    keyToNode.emplace_back(key, *node);
                }

                // Verify repeated lookups yield identical results
                for (int round = 0; round < 5; ++round) {
                    for (const auto& [key, expectedNode] : keyToNode) {
                        const auto actualNode = ring.getNode(key);
                        if (!actualNode.has_value() || *actualNode != expectedNode) {
                            throw std::runtime_error("Non-deterministic lookup for key: " + key);
                        }
                    }
                }
            }
        },
        {
            "HashRingAddNodeMinimalDisruption",
            []() {
                HashRing ring;
                ring.addNode("node-1", 150);
                ring.addNode("node-2", 150);
                ring.addNode("node-3", 150);

                const int numKeys = 1200;
                std::vector<std::pair<std::string, std::string>> initialMappings;
                initialMappings.reserve(numKeys);

                for (int i = 0; i < numKeys; ++i) {
                    const std::string key = "key_dist_" + std::to_string(i);
                    const auto node = ring.getNode(key);
                    if (!node.has_value()) {
                        throw std::runtime_error("Lookup failed for " + key);
                    }
                    initialMappings.emplace_back(key, *node);
                }

                // Add a 4th node. Theoretical redistribution is ~1/(N+1) = 25%.
                if (!ring.addNode("node-4", 150)) {
                    throw std::runtime_error("Failed to add node-4");
                }

                int remappedCount = 0;
                for (const auto& [key, previousNode] : initialMappings) {
                    const auto newNode = ring.getNode(key);
                    if (!newNode.has_value()) {
                        throw std::runtime_error("Lookup failed after adding node for " + key);
                    }

                    if (*newNode != previousNode) {
                        remappedCount++;
                        // Monotonicity property: Any key that moves MUST move to the newly added node.
                        if (*newNode != "node-4") {
                            throw std::runtime_error("Key moved to an existing node rather than node-4: " + key);
                        }
                    }
                }

                const double remappedRatio = static_cast<double>(remappedCount) / numKeys;
                // Consistent hashing with 150 vnodes should remap between 15% and 35% of keys (centered around 25%)
                if (remappedRatio < 0.15 || remappedRatio > 0.35) {
                    throw std::runtime_error("Redistribution ratio out of expected range (0.15 - 0.35): " +
                                             std::to_string(remappedRatio));
                }
            }
        },
        {
            "HashRingRemoveNode",
            []() {
                HashRing ring;
                ring.addNode("srv-1", 150);
                ring.addNode("srv-2", 150);
                ring.addNode("srv-3", 150);

                const int numKeys = 1200;
                std::vector<std::pair<std::string, std::string>> initialMappings;
                initialMappings.reserve(numKeys);

                for (int i = 0; i < numKeys; ++i) {
                    const std::string key = "removal_key_" + std::to_string(i);
                    const auto node = ring.getNode(key);
                    initialMappings.emplace_back(key, *node);
                }

                if (!ring.removeNode("srv-2")) {
                    throw std::runtime_error("Failed to remove srv-2");
                }
                if (ring.hasNode("srv-2")) {
                    throw std::runtime_error("srv-2 should not be present after removal");
                }
                if (ring.nodeCount() != 2) {
                    throw std::runtime_error("Node count should be 2 after removal");
                }

                for (const auto& [key, previousNode] : initialMappings) {
                    const auto currentNode = ring.getNode(key);
                    if (!currentNode.has_value()) {
                        throw std::runtime_error("Lookup failed after removal for " + key);
                    }
                    if (*currentNode == "srv-2") {
                        throw std::runtime_error("Key still routes to removed node srv-2: " + key);
                    }

                    // Keys originally on surviving nodes must remain on the same surviving node
                    if (previousNode == "srv-1" && *currentNode != "srv-1") {
                        throw std::runtime_error("Key on srv-1 moved unexpectedly: " + key);
                    }
                    if (previousNode == "srv-3" && *currentNode != "srv-3") {
                        throw std::runtime_error("Key on srv-3 moved unexpectedly: " + key);
                    }
                }
            }
        },
        {
            "HashRingVirtualNodeBehavior",
            []() {
                // Ring with 1 virtual node per physical node
                HashRing singleVnodeRing;
                singleVnodeRing.addNode("node-A", 1);
                singleVnodeRing.addNode("node-B", 1);
                singleVnodeRing.addNode("node-C", 1);

                // Ring with 150 virtual nodes per physical node
                HashRing multiVnodeRing;
                multiVnodeRing.addNode("node-A", 150);
                multiVnodeRing.addNode("node-B", 150);
                multiVnodeRing.addNode("node-C", 150);

                const int numKeys = 3000;
                std::map<std::string, int> singleVnodeCounts;
                std::map<std::string, int> multiVnodeCounts;

                for (int i = 0; i < numKeys; ++i) {
                    const std::string key = "vnode_sample_key_" + std::to_string(i);
                    singleVnodeCounts[*singleVnodeRing.getNode(key)]++;
                    multiVnodeCounts[*multiVnodeRing.getNode(key)]++;
                }

                // Compute standard deviation for single vnode
                const double mean = static_cast<double>(numKeys) / 3.0; // 1000 keys expected
                double singleVariance = 0.0;
                for (const auto& [node, count] : singleVnodeCounts) {
                    singleVariance += (count - mean) * (count - mean);
                }
                const double singleStdDev = std::sqrt(singleVariance / 3.0);

                // Compute standard deviation for multi vnode
                double multiVariance = 0.0;
                for (const auto& [node, count] : multiVnodeCounts) {
                    multiVariance += (count - mean) * (count - mean);
                    // With 150 vnodes, each node should receive between 25% (750) and 42% (1260) of keys
                    if (count < 750 || count > 1260) {
                        throw std::runtime_error("Multi-vnode allocation skewed: node " + node +
                                                 " received " + std::to_string(count) + " keys");
                    }
                }
                const double multiStdDev = std::sqrt(multiVariance / 3.0);

                // Multi-vnode standard deviation must be substantially lower than single vnode
                if (multiStdDev >= singleStdDev) {
                    throw std::runtime_error("Expected multiVnode std dev (" + std::to_string(multiStdDev) +
                                             ") to be lower than singleVnode std dev (" +
                                             std::to_string(singleStdDev) + ")");
                }
            }
        },
        {
            "HashRingDuplicateAndInvalidInputs",
            []() {
                HashRing ring;

                // Empty node ID
                if (ring.addNode("")) {
                    throw std::runtime_error("addNode with empty ID must return false");
                }
                // Valid node
                if (!ring.addNode("node-X", 100)) {
                    throw std::runtime_error("Failed to add valid node-X");
                }
                // Duplicate node ID
                if (ring.addNode("node-X", 100)) {
                    throw std::runtime_error("Duplicate addNode must return false");
                }
                if (ring.addNode("node-X", 50)) {
                    throw std::runtime_error("Duplicate addNode with different vnodes must return false");
                }

                // Zero vnodes specified should default to DEFAULT_VNODES
                if (!ring.addNode("node-Y", 0)) {
                    throw std::runtime_error("addNode with 0 vnodes should succeed with default vnodes");
                }
                if (ring.ringSize() != 100 + HashRing::DEFAULT_VNODES) {
                    throw std::runtime_error("ringSize after default vnode addition is incorrect");
                }

                // Invalid removal
                if (ring.removeNode("")) {
                    throw std::runtime_error("removeNode with empty ID must return false");
                }
                if (ring.removeNode("nonexistent-node")) {
                    throw std::runtime_error("removeNode on non-existent node must return false");
                }

                // Empty key lookup
                if (ring.getNode("").has_value()) {
                    throw std::runtime_error("getNode on empty key must return nullopt");
                }

                // Clear
                ring.clear();
                if (!ring.empty()) {
                    throw std::runtime_error("Ring must be empty after clear()");
                }
                if (ring.nodeCount() != 0 || ring.ringSize() != 0) {
                    throw std::runtime_error("Node and ring counts must be 0 after clear()");
                }

                // Re-add previously registered node
                if (!ring.addNode("node-X", 100)) {
                    throw std::runtime_error("Failed to re-add node-X after clear()");
                }
            }
        },
        {
            "HashRingThreadSafetyConcurrentReads",
            []() {
                HashRing ring;
                ring.addNode("node-1", 100);
                ring.addNode("node-2", 100);
                ring.addNode("node-3", 100);

                constexpr int threadCount = 4;
                constexpr int lookupsPerThread = 2000;
                std::vector<std::thread> threads;
                threads.reserve(threadCount);

                for (int t = 0; t < threadCount; ++t) {
                    threads.emplace_back([&ring, t]() {
                        for (int i = 0; i < lookupsPerThread; ++i) {
                            const std::string key = "key_" + std::to_string(t) + "_" + std::to_string(i);
                            const auto node = ring.getNode(key);
                            if (!node.has_value()) {
                                throw std::runtime_error("Concurrent read returned nullopt for key: " + key);
                            }
                        }
                    });
                }

                for (auto& th : threads) {
                    th.join();
                }
            }
        }
    };
}

} // namespace ringcache::test
