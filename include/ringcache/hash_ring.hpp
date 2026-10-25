#pragma once

#include "ringcache/hash.hpp"

#include <cstddef>
#include <cstdint>
#include <map>
#include <optional>
#include <shared_mutex>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace ringcache {

/// @brief Thread-safe consistent hash ring with virtual node distribution.
///
/// Maps cache keys and physical nodes onto a circular 64-bit token space [0, 2^64 - 1].
/// Physical nodes are replicated across the ring as V virtual nodes (vnodes) to ensure
/// statistically uniform key distribution and prevent hot spotting.
class HashRing {
public:
    /// @brief Default number of virtual nodes allocated per physical node if unspecified.
    static constexpr std::size_t DEFAULT_VNODES = 150;

    /// @brief Construct a consistent hash ring.
    /// @param defaultVnodes Number of virtual nodes to assign to each physical node by default.
    /// @param hashFn Callable 64-bit hash function (defaults to defaultHash64).
    explicit HashRing(std::size_t defaultVnodes = DEFAULT_VNODES,
                      HashFunction hashFn = defaultHash64);

    ~HashRing() = default;

    // Non-copyable and non-movable to ensure safe concurrency with internal synchronization.
    HashRing(const HashRing&) = delete;
    HashRing& operator=(const HashRing&) = delete;
    HashRing(HashRing&&) = delete;
    HashRing& operator=(HashRing&&) = delete;

    /// @brief Add a physical node to the ring with a specified number of virtual nodes.
    /// @param nodeId Unique identifier for the physical node (e.g., "node-1" or "10.0.0.1:8080").
    /// @param vnodes Number of virtual nodes (if 0, uses defaultVnodes).
    /// @return true if the node was successfully added; false if nodeId is empty or already registered.
    bool addNode(const std::string& nodeId, std::size_t vnodes = 0);

    /// @brief Remove a physical node and all of its associated virtual nodes from the ring.
    /// @param nodeId Identifier of the node to remove.
    /// @return true if the node was removed; false if nodeId was empty or not found.
    bool removeNode(const std::string& nodeId);

    /// @brief Check whether a physical node is currently present on the ring.
    /// @param nodeId Identifier of the node to check.
    /// @return true if present; false otherwise.
    [[nodiscard]] bool hasNode(const std::string& nodeId) const;

    /// @brief Deterministically find the node responsible for a given key.
    ///
    /// Locates the first node whose ring token is greater than or equal to the key's hash
    /// in clockwise order. If the hash exceeds the highest token on the ring, wraps around
    /// to the lowest token on the ring.
    ///
    /// @param key The cache key to look up.
    /// @return The responsible node identifier, or std::nullopt if the ring is empty or key is empty.
    [[nodiscard]] std::optional<std::string> getNode(std::string_view key) const;

    /// @brief Return the number of distinct physical nodes on the ring.
    [[nodiscard]] std::size_t nodeCount() const;

    /// @brief Return the total number of virtual node tokens placed on the ring.
    [[nodiscard]] std::size_t ringSize() const;

    /// @brief Check whether the ring has any registered nodes.
    [[nodiscard]] bool empty() const;

    /// @brief Get a list of all currently registered physical node identifiers.
    [[nodiscard]] std::vector<std::string> getNodes() const;

    /// @brief Remove all nodes and tokens from the ring.
    void clear();

private:
    std::size_t defaultVnodes_;
    HashFunction hashFn_;

    mutable std::shared_mutex mutex_;

    // Ordered ring mapping 64-bit token -> physical node ID
    std::map<uint64_t, std::string> ring_;

    // Map physical node ID -> vector of ring tokens assigned to that node
    std::unordered_map<std::string, std::vector<uint64_t>> nodeTokens_;
};

} // namespace ringcache
