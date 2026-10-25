#pragma once

#include "ringcache/cluster_router.hpp"
#include "ringcache/hash_ring.hpp"
#include "ringcache/network_client.hpp"

#include <chrono>
#include <cstddef>
#include <memory>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ringcache {

/// @brief Orchestrates a distributed cache cluster across separate OS processes over TCP.
///
/// Features:
/// - Consistent hash ring routing with virtual nodes.
/// - Primary + clockwise successor replica node selection (replication factor R).
/// - Primary write with TCP replica propagation and detailed WriteResult metrics.
/// - Primary-first read with TCP replica fallback.
/// - Bounded timeouts, automatic reconnection, and thread-safe routing.
class NetworkClusterRouter {
public:
    explicit NetworkClusterRouter(std::size_t replicationFactor = 1,
                                 std::size_t defaultVnodes = HashRing::DEFAULT_VNODES,
                                 std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    ~NetworkClusterRouter() = default;

    NetworkClusterRouter(const NetworkClusterRouter&) = delete;
    NetworkClusterRouter& operator=(const NetworkClusterRouter&) = delete;
    NetworkClusterRouter(NetworkClusterRouter&&) = delete;
    NetworkClusterRouter& operator=(NetworkClusterRouter&&) = delete;

    /// @brief Register a remote node endpoint in the cluster and hash ring.
    /// @param nodeId Unique identifier for the node (e.g. "node-1").
    /// @param host IP address or hostname of the node.
    /// @param port TCP port the node is listening on.
    /// @param vnodes Virtual node count (if 0, uses defaultVnodes).
    /// @return true if added; false if duplicate or invalid.
    bool addNode(const std::string& nodeId,
                 const std::string& host,
                 uint16_t port,
                 std::size_t vnodes = 0);

    /// @brief Remove a node from the cluster and hash ring.
    bool removeNode(const std::string& nodeId);

    /// @brief Check whether a node is registered.
    [[nodiscard]] bool hasNode(const std::string& nodeId) const;

    /// @brief Return the number of registered physical nodes.
    [[nodiscard]] std::size_t nodeCount() const;

    /// @brief Return all registered physical node IDs.
    [[nodiscard]] std::vector<std::string> getNodeIds() const;

    /// @brief Get the configured replication factor.
    [[nodiscard]] std::size_t getReplicationFactor() const noexcept;

    /// @brief Update the replication factor.
    void setReplicationFactor(std::size_t r) noexcept;

    /// @brief Return target physical node IDs for a key (index 0 is primary, followed by replicas).
    [[nodiscard]] std::vector<std::string> getTargetNodes(std::string_view key) const;

    /// @brief Store a key-value pair across primary and replica nodes over TCP.
    /// @return true if successfully written to the primary node; false otherwise.
    bool put(const std::string& key, const std::string& value);

    /// @brief Alias for put.
    bool set(const std::string& key, const std::string& value);

    /// @brief Store a key-value pair and return comprehensive write metrics.
    [[nodiscard]] WriteResult putWithDetails(const std::string& key, const std::string& value);

    /// @brief Retrieve a key's value over TCP (primary-first with replica fallback).
    [[nodiscard]] std::optional<std::string> get(const std::string& key);

    /// @brief Delete a key from primary and all replica nodes over TCP.
    /// @return true if deleted from at least one node; false otherwise.
    bool del(const std::string& key);

    /// @brief Alias for del.
    bool remove(const std::string& key);

    /// @brief Probe a specific node to verify responsiveness.
    bool pingNode(const std::string& nodeId);

    /// @brief Get the configured endpoint for a registered node.
    [[nodiscard]] std::optional<NodeEndpoint> getNodeEndpoint(const std::string& nodeId) const;

private:
    std::shared_ptr<NetworkClient> getOrCreateClient(const std::string& nodeId);

    std::size_t replicationFactor_;
    std::size_t defaultVnodes_;
    std::chrono::milliseconds timeout_;

    mutable std::shared_mutex mutex_;
    HashRing ring_;
    std::unordered_map<std::string, NodeEndpoint> endpoints_;
    std::unordered_map<std::string, std::shared_ptr<NetworkClient>> clients_;
};

} // namespace ringcache
