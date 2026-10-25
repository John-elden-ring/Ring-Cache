#pragma once

#include <cstddef>
#include <iterator>
#include <list>
#include <mutex>
#include <optional>
#include <string>
#include <unordered_map>
#include <utility>

namespace ringcache {

/// @brief Thread-safe in-memory Least Recently Used (LRU) cache.
///
/// Discards the least recently accessed entries when maximum capacity is exceeded.
/// All core operations (put, get, remove) execute in O(1) average time complexity.
///
/// Thread Safety:
/// All public operations are synchronized with an internal std::mutex to protect
/// against data races. Read access (get) mutates the internal LRU recency order
/// and is protected under the same mutual exclusion lock.
template <typename Key = std::string, typename Value = std::string, typename Hash = std::hash<Key>>
class BasicLruCache {
public:
    struct Entry {
        Key key;
        Value value;
    };

    using ListType = std::list<Entry>;
    using ListIterator = typename ListType::iterator;
    using MapType = std::unordered_map<Key, ListIterator, Hash>;
    using MapIterator = typename MapType::iterator;

    /// @brief Construct an LRU cache with the specified maximum capacity.
    /// @param capacity Maximum number of entries the cache can hold. If 0, the cache
    ///                 holds no entries and all insertions are safely rejected.
    explicit BasicLruCache(std::size_t capacity) noexcept
        : capacity_(capacity) {}

    ~BasicLruCache() = default;

    // Non-copyable and non-movable to ensure safe concurrency.
    BasicLruCache(const BasicLruCache&) = delete;
    BasicLruCache& operator=(const BasicLruCache&) = delete;
    BasicLruCache(BasicLruCache&&) = delete;
    BasicLruCache& operator=(BasicLruCache&&) = delete;

    /// @brief Insert or update a key-value pair in the cache.
    ///
    /// If capacity is 0, this operation is a safe no-op and returns false.
    /// If the key exists, updates its value and marks it as Most Recently Used (MRU).
    /// If the key does not exist and the cache is at capacity, the Least Recently Used (LRU)
    /// entry at the tail is evicted before inserting the new entry at the head.
    ///
    /// @param key The key to insert or update.
    /// @param value The value associated with the key.
    /// @return true if the item was stored/updated; false if capacity is 0.
    bool put(const Key& key, const Value& value) {
        if (capacity_ == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        MapIterator it = map_.find(key);
        if (it != map_.end()) {
            it->second->value = value;
            items_.splice(items_.begin(), items_, it->second);
            return true;
        }

        if (items_.size() >= capacity_) {
            evictLruLocked();
        }

        items_.push_front(Entry{key, value});
        map_.emplace(items_.front().key, items_.begin());
        return true;
    }

    /// @brief Insert or update a key-value pair using rvalue references.
    bool put(Key&& key, Value&& value) {
        if (capacity_ == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        MapIterator it = map_.find(key);
        if (it != map_.end()) {
            it->second->value = std::move(value);
            items_.splice(items_.begin(), items_, it->second);
            return true;
        }

        if (items_.size() >= capacity_) {
            evictLruLocked();
        }

        items_.push_front(Entry{std::move(key), std::move(value)});
        map_.emplace(items_.front().key, items_.begin());
        return true;
    }

    /// @brief Retrieve an entry by key and mark it as Most Recently Used (MRU).
    /// @param key The key to look up.
    /// @return std::optional containing the value if found; std::nullopt if missing or capacity is 0.
    [[nodiscard]] std::optional<Value> get(const Key& key) {
        if (capacity_ == 0) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        MapIterator it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }

        // Move the accessed item to the front (MRU)
        items_.splice(items_.begin(), items_, it->second);
        return it->second->value;
    }

    /// @brief Remove an entry from the cache by key.
    /// @param key The key to remove.
    /// @return true if the key was found and removed; false if missing or capacity is 0.
    bool remove(const Key& key) {
        if (capacity_ == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        MapIterator it = map_.find(key);
        if (it == map_.end()) {
            return false;
        }

        items_.erase(it->second);
        map_.erase(it);
        return true;
    }

    /// @brief Check whether a key is present without modifying access recency order.
    /// @param key The key to check.
    /// @return true if the key is present; false otherwise.
    [[nodiscard]] bool contains(const Key& key) const {
        if (capacity_ == 0) {
            return false;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        return map_.find(key) != map_.end();
    }

    /// @brief Inspect a key's value without modifying access recency order.
    /// @param key The key to look up.
    /// @return std::optional containing the value if found; std::nullopt if missing or capacity is 0.
    [[nodiscard]] std::optional<Value> peek(const Key& key) const {
        if (capacity_ == 0) {
            return std::nullopt;
        }

        std::lock_guard<std::mutex> lock(mutex_);
        const auto it = map_.find(key);
        if (it == map_.end()) {
            return std::nullopt;
        }
        return it->second->value;
    }

    /// @brief Return the number of items currently stored in the cache.
    [[nodiscard]] std::size_t size() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.size();
    }

    /// @brief Return the configured maximum capacity of the cache.
    [[nodiscard]] std::size_t capacity() const noexcept {
        return capacity_;
    }

    /// @brief Check whether the cache contains zero elements.
    [[nodiscard]] bool empty() const {
        std::lock_guard<std::mutex> lock(mutex_);
        return items_.empty();
    }

    /// @brief Remove all entries from the cache.
    void clear() {
        std::lock_guard<std::mutex> lock(mutex_);
        items_.clear();
        map_.clear();
    }

private:
    void evictLruLocked() {
        if (!items_.empty()) {
            const ListIterator lruIt = std::prev(items_.end());
            map_.erase(lruIt->key);
            items_.pop_back();
        }
    }

    const std::size_t capacity_;
    mutable std::mutex mutex_;

    ListType items_;
    MapType map_;
};

/// @brief Standard string-to-string LRU cache.
using LruCache = BasicLruCache<std::string, std::string>;

} // namespace ringcache
