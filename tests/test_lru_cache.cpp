#include "ringcache/lru_cache.hpp"
#include "test_common.hpp"

#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getLruCacheTests() {
    return {
        {
            "LruCacheInsertionAndRetrieval",
            []() {
                LruCache cache(3);
                if (cache.capacity() != 3) {
                    throw std::runtime_error("Capacity should be 3");
                }
                if (!cache.empty()) {
                    throw std::runtime_error("New cache must be empty");
                }

                if (!cache.put("k1", "v1")) {
                    throw std::runtime_error("Failed to put k1");
                }
                if (!cache.put("k2", "v2")) {
                    throw std::runtime_error("Failed to put k2");
                }

                if (cache.size() != 2) {
                    throw std::runtime_error("Cache size should be 2");
                }
                if (cache.empty()) {
                    throw std::runtime_error("Cache should not be empty");
                }

                const auto v1 = cache.get("k1");
                if (!v1.has_value() || *v1 != "v1") {
                    throw std::runtime_error("Expected k1 -> v1");
                }

                const auto v2 = cache.get("k2");
                if (!v2.has_value() || *v2 != "v2") {
                    throw std::runtime_error("Expected k2 -> v2");
                }

                if (cache.contains("k3")) {
                    throw std::runtime_error("Cache should not contain uninserted key k3");
                }
                if (cache.get("k3").has_value()) {
                    throw std::runtime_error("get on missing key should return nullopt");
                }
            }
        },
        {
            "LruCacheUpdateExistingKey",
            []() {
                LruCache cache(2);
                cache.put("k1", "v1");
                cache.put("k2", "v2");

                // Update k1
                if (!cache.put("k1", "v1_updated")) {
                    throw std::runtime_error("Failed to update k1");
                }

                // Size must not increase
                if (cache.size() != 2) {
                    throw std::runtime_error("Size should remain 2 after update");
                }

                const auto v1 = cache.get("k1");
                if (!v1.has_value() || *v1 != "v1_updated") {
                    throw std::runtime_error("Expected k1 -> v1_updated");
                }

                // Updating k1 made it MRU. Next insert should evict k2 (the LRU entry)
                cache.put("k3", "v3");

                if (cache.contains("k2")) {
                    throw std::runtime_error("k2 should have been evicted");
                }
                if (!cache.contains("k1") || !cache.contains("k3")) {
                    throw std::runtime_error("k1 and k3 should remain in the cache");
                }
            }
        },
        {
            "LruCacheEvictionOrder",
            []() {
                LruCache cache(3);
                cache.put("a", "1");
                cache.put("b", "2");
                cache.put("c", "3");

                // Inserting 4th item must evict 'a' (oldest inserted, never accessed)
                cache.put("d", "4");

                if (cache.contains("a")) {
                    throw std::runtime_error("'a' should have been evicted first");
                }
                if (!cache.contains("b") || !cache.contains("c") || !cache.contains("d")) {
                    throw std::runtime_error("b, c, d must still be present");
                }

                // Inserting 5th item must evict 'b'
                cache.put("e", "5");

                if (cache.contains("b")) {
                    throw std::runtime_error("'b' should have been evicted second");
                }
                if (!cache.contains("c") || !cache.contains("d") || !cache.contains("e")) {
                    throw std::runtime_error("c, d, e must still be present");
                }
            }
        },
        {
            "LruCacheAccessChangesRecency",
            []() {
                LruCache cache(3);
                cache.put("a", "1");
                cache.put("b", "2");
                cache.put("c", "3");

                // Access 'a' -> 'a' is now MRU; order becomes: b (LRU), c, a (MRU)
                const auto valA = cache.get("a");
                if (!valA.has_value() || *valA != "1") {
                    throw std::runtime_error("Failed to read 'a'");
                }

                // Next insertion must evict 'b' (not 'a')
                cache.put("d", "4");

                if (cache.contains("b")) {
                    throw std::runtime_error("'b' should have been evicted as the LRU entry");
                }
                if (!cache.contains("a")) {
                    throw std::runtime_error("'a' should have survived because it was accessed");
                }
                if (!cache.contains("c") || !cache.contains("d")) {
                    throw std::runtime_error("'c' and 'd' should remain");
                }
            }
        },
        {
            "LruCacheRemoveOperations",
            []() {
                LruCache cache(3);
                cache.put("x", "10");
                cache.put("y", "20");

                // Removing existing key
                if (!cache.remove("x")) {
                    throw std::runtime_error("remove('x') should return true");
                }
                if (cache.size() != 1) {
                    throw std::runtime_error("Size should be 1 after removing x");
                }
                if (cache.contains("x")) {
                    throw std::runtime_error("x must not be contained after removal");
                }
                if (cache.get("x").has_value()) {
                    throw std::runtime_error("get('x') must return nullopt after removal");
                }

                // Removing already removed key
                if (cache.remove("x")) {
                    throw std::runtime_error("Second remove('x') must return false");
                }

                // Removing missing key
                if (cache.remove("missing")) {
                    throw std::runtime_error("remove on missing key must return false");
                }
            }
        },
        {
            "LruCacheCapacityOne",
            []() {
                LruCache cache(1);
                if (cache.capacity() != 1) {
                    throw std::runtime_error("Capacity must be 1");
                }

                cache.put("first", "val1");
                if (cache.size() != 1 || cache.get("first") != "val1") {
                    throw std::runtime_error("Failed to store first entry");
                }

                // Update in place
                cache.put("first", "val1_updated");
                if (cache.size() != 1 || cache.get("first") != "val1_updated") {
                    throw std::runtime_error("Failed to update first entry");
                }

                // Insert second entry: immediately evicts first
                cache.put("second", "val2");
                if (cache.size() != 1) {
                    throw std::runtime_error("Size must remain 1 after second put");
                }
                if (cache.contains("first")) {
                    throw std::runtime_error("'first' should be evicted");
                }
                if (cache.get("second") != "val2") {
                    throw std::runtime_error("'second' must be present");
                }

                // Remove
                if (!cache.remove("second")) {
                    throw std::runtime_error("remove('second') must return true");
                }
                if (!cache.empty() || cache.size() != 0) {
                    throw std::runtime_error("Cache must be empty after removal");
                }
            }
        },
        {
            "LruCacheCapacityZero",
            []() {
                LruCache cache(0);
                if (cache.capacity() != 0) {
                    throw std::runtime_error("Capacity must be 0");
                }
                if (!cache.empty() || cache.size() != 0) {
                    throw std::runtime_error("Capacity zero cache must be empty");
                }

                // Put should safely fail and return false
                if (cache.put("key", "val")) {
                    throw std::runtime_error("put on capacity 0 must return false");
                }
                if (cache.size() != 0) {
                    throw std::runtime_error("Size must remain 0 on capacity 0 cache");
                }

                // Get should return nullopt
                if (cache.get("key").has_value()) {
                    throw std::runtime_error("get on capacity 0 must return nullopt");
                }

                // Contains should return false
                if (cache.contains("key")) {
                    throw std::runtime_error("contains on capacity 0 must return false");
                }

                // Remove should return false
                if (cache.remove("key")) {
                    throw std::runtime_error("remove on capacity 0 must return false");
                }
            }
        },
        {
            "LruCacheEmptyKeysAndValues",
            []() {
                LruCache cache(2);

                // Empty key
                if (!cache.put("", "value_for_empty_key")) {
                    throw std::runtime_error("Failed to put empty key");
                }
                if (cache.get("") != "value_for_empty_key") {
                    throw std::runtime_error("Failed to get empty key");
                }

                // Empty value
                if (!cache.put("empty_value_key", "")) {
                    throw std::runtime_error("Failed to put empty value");
                }
                if (cache.get("empty_value_key") != "") {
                    throw std::runtime_error("Failed to get empty value");
                }

                // Both empty
                if (!cache.put("", "")) {
                    throw std::runtime_error("Failed to update empty key with empty value");
                }
                if (cache.get("") != "") {
                    throw std::runtime_error("Expected empty string for empty key");
                }
            }
        },
        {
            "LruCacheRepeatedOperationsAndChurn",
            []() {
                constexpr std::size_t cap = 20;
                LruCache cache(cap);

                // Run 1000 cyclic operations
                for (int i = 0; i < 1000; ++i) {
                    const std::string key = "key_" + std::to_string(i % 50);
                    const std::string val = "val_" + std::to_string(i);

                    cache.put(key, val);

                    if (cache.size() > cap) {
                        throw std::runtime_error("Cache size exceeded capacity during churn!");
                    }

                    if (i % 3 == 0) {
                        static_cast<void>(cache.get("key_" + std::to_string((i * 7) % 50)));
                    }

                    if (i % 7 == 0) {
                        static_cast<void>(cache.remove("key_" + std::to_string((i * 3) % 50)));
                    }
                }

                if (cache.size() > cap) {
                    throw std::runtime_error("Final cache size exceeds capacity");
                }
            }
        },
        {
            "LruCacheThreadSafetyConcurrentAccess",
            []() {
                constexpr std::size_t cap = 50;
                LruCache cache(cap);

                constexpr int threadCount = 4;
                constexpr int opsPerThread = 2000;
                std::vector<std::thread> threads;
                threads.reserve(threadCount);

                for (int t = 0; t < threadCount; ++t) {
                    threads.emplace_back([&cache, t]() {
                        for (int i = 0; i < opsPerThread; ++i) {
                            const std::string key = "shared_key_" + std::to_string((t * 20 + i) % 80);
                            const std::string val = "shared_val_" + std::to_string(i);

                            if (i % 4 == 0) {
                                static_cast<void>(cache.put(key, val));
                            } else if (i % 4 == 1) {
                                static_cast<void>(cache.get(key));
                            } else if (i % 4 == 2) {
                                static_cast<void>(cache.contains(key));
                            } else {
                                static_cast<void>(cache.remove(key));
                            }
                        }
                    });
                }

                for (auto& th : threads) {
                    th.join();
                }

                if (cache.size() > cap) {
                    throw std::runtime_error("Cache size exceeded capacity under concurrent access: " +
                                             std::to_string(cache.size()));
                }
            }
        }
    };
}

} // namespace ringcache::test
