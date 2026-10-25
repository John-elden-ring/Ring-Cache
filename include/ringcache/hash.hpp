#pragma once

#include <cstdint>
#include <functional>
#include <string_view>

namespace ringcache {

/// @brief Deterministic 64-bit non-cryptographic hash function using FNV-1a with bit mixing.
/// @param key The input string to hash.
/// @return 64-bit hash value distributed across [0, 2^64 - 1].
[[nodiscard]] uint64_t defaultHash64(std::string_view key) noexcept;

/// @brief Function signature for custom hash functions passed to the hash ring.
using HashFunction = std::function<uint64_t(std::string_view)>;

} // namespace ringcache
