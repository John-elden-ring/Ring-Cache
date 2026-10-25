#pragma once

#include "ringcache/cache_node.hpp"
#include "ringcache/hash_ring.hpp"

#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ringcache {

/// @brief Result metadata for write operations across primary and replica nodes.
struct WriteResult {
    bool success{false};
    std::size_t writtenCount{0};
    std::size_t targetCount{0};
    std::string primaryNode;
    std::vector<std::string> replicaNodes;
};

/// @brief Orchestrates an in-process distributed cache cluster using consistent hashing and replication.
///
/// Features:
/// - Primary + successor replica routing via consistent hashing ring.
/// - Configurable replication factor R with physical node deduplication.
/// - Primary-first read routing with replica fallback.
/// - Thread-safe operations and cluster topology mutation.
/// - Safe non-mutating inspection methods for testing and verification.
class ClusterRouter {
public:
    /// @brief Construct a cluster router with specified replication factor and vnode count.
    /// @param replicationFactor Number of distinct physical nodes to store each key (default 1).
    /// @param defaultVnodes Virtual nodes allocated per physical node on the ring (default 150).
    explicit ClusterRouter(std::size_t replicationFactor = 1,
                           std::size_t defaultVnodes = HashRing::DEFAULT_VNODES);

    ~ClusterRouter() = default;

    ClusterRouter(const ClusterRouter&) = delete;
    ClusterRouter& operator=(const ClusterRouter&) = delete;
    ClusterRouter(ClusterRouter&&) = delete;
    ClusterRouter& operator=(ClusterRouter&&) = delete;

    /// @brief Register a new cache node in the cluster.
    /// @param nodeId Unique identifier for the node.
    /// @param cacheCapacity Maximum entries for this node's LRU cache.
    /// @param vnodes Number of virtual nodes on the hash ring (if 0, uses defaultVnodes).
    /// @return true if added; false if nodeId is empty or duplicate.
    bool addNode(const std::string& nodeId, std::size_t cacheCapacity, std::size_t vnodes = 0);

    /// @brief Remove a cache node from the cluster and the hash ring.
    /// @param nodeId Unique identifier of the node to remove.
    /// @return true if removed; false if not found or empty.
    bool removeNode(const std::string& nodeId);

    /// @brief Check whether a node is currently registered in the cluster.
    [[nodiscard]] bool hasNode(const std::string& nodeId) const;

    /// @brief Return the number of physical nodes currently active in the cluster.
    [[nodiscard]] std::size_t nodeCount() const;

    /// @brief Get all registered physical node identifiers.
    [[nodiscard]] std::vector<std::string> getNodeIds() const;

    /// @brief Get the configured replication factor.
    [[nodiscard]] std::size_t getReplicationFactor() const noexcept;

    /// @brief Update the replication factor.
    void setReplicationFactor(std::size_t r) noexcept;

    /// @brief Determine the primary and replica node IDs that a key routes to.
    /// @param key The key to route.
    /// @return Ordered list of distinct physical node IDs (index 0 is primary).
    [[nodiscard]] std::vector<std::string> getTargetNodes(std::string_view key) const;

    /// @brief Store a key-value pair across primary and replica nodes.
    ///
    /// Success requires the primary node to store the value. If primary succeeds,
    /// writes are propagated to all available replicas.
    ///
    /// @return true if stored on primary; false if primary failed, cluster empty, or R=0.
    bool set(const std::string& key, const std::string& value);

    /// @brief Alias for set(key, value).
    bool put(const std::string& key, const std::string& value);

    /// @brief Store a key-value pair and return comprehensive write details.
    [[nodiscard]] WriteResult setWithDetails(const std::string& key, const std::string& value);

    /// @brief Retrieve a value by key.
    ///
    /// Attempts read on primary node first. If not found (e.g. evicted on primary),
    /// falls back to replicas in clockwise ring order.
    ///
    /// @return std::optional with value if found; std::nullopt otherwise.
    [[nodiscard]] std::optional<std::string> get(const std::string& key);

    /// @brief Delete a key from primary and all replica nodes.
    /// @return true if the key was deleted from at least one node; false otherwise.
    bool del(const std::string& key);

    /// @brief Alias for del(key).
    bool remove(const std::string& key);

    /// @brief Inspect a key's value directly on a specific node without altering LRU recency.
    [[nodiscard]] std::optional<std::string> peekNodeKey(const std::string& nodeId,
                                                         const std::string& key) const;

    /// @brief Check whether a specific node currently contains a key without altering recency.
    [[nodiscard]] bool nodeContains(const std::string& nodeId, const std::string& key) const;

    /// @brief Return the number of items stored on a specific node.
    [[nodiscard]] std::optional<std::size_t> getNodeSize(const std::string& nodeId) const;

    /// @brief Clear all entries across all active cache nodes.
    void clearAllNodes();

private:
    std::size_t replicationFactor_;
    std::size_t defaultVnodes_;

    mutable std::shared_mutex mutex_;
    HashRing ring_;
    std::unordered_map<std::string, std::shared_ptr<CacheNode>> nodes_;
};

} // namespace ringcache
