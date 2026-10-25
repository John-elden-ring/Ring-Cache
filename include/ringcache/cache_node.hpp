#pragma once

#include "ringcache/lru_cache.hpp"

#include <cstddef>
#include <optional>
#include <string>

namespace ringcache {

/// @brief Represents an individual cache node in a distributed cache cluster.
///
/// Owns a unique node identifier and an in-memory thread-safe LRU cache.
class CacheNode {
public:
    /// @brief Construct a cache node with an identifier and LRU cache capacity.
    /// @param id Unique identifier of the node (e.g. "node-1").
    /// @param capacity Maximum number of entries the node's LRU cache can hold.
    explicit CacheNode(std::string id, std::size_t capacity);

    ~CacheNode() = default;

    CacheNode(const CacheNode&) = delete;
    CacheNode& operator=(const CacheNode&) = delete;
    CacheNode(CacheNode&&) = delete;
    CacheNode& operator=(CacheNode&&) = delete;

    /// @brief Get the unique identifier of this node.
    [[nodiscard]] const std::string& getId() const noexcept;

    /// @brief Insert or update a key-value pair in this node's cache.
    bool put(const std::string& key, const std::string& value);

    /// @brief Retrieve an entry by key, updating access recency.
    [[nodiscard]] std::optional<std::string> get(const std::string& key);

    /// @brief Remove an entry by key from this node.
    bool remove(const std::string& key);

    /// @brief Check whether a key is present without altering recency.
    [[nodiscard]] bool contains(const std::string& key) const;

    /// @brief Inspect a key's value without altering recency.
    [[nodiscard]] std::optional<std::string> peek(const std::string& key) const;

    /// @brief Return the number of items stored on this node.
    [[nodiscard]] std::size_t size() const;

    /// @brief Return the maximum capacity of this node's cache.
    [[nodiscard]] std::size_t capacity() const noexcept;

    /// @brief Check whether this node's cache is empty.
    [[nodiscard]] bool empty() const;

    /// @brief Clear all entries from this node's cache.
    void clear();

private:
    std::string id_;
    LruCache cache_;
};

} // namespace ringcache
