#include "ringcache/hash.hpp"

namespace ringcache {

namespace {

constexpr uint64_t FNV_OFFSET_BASIS = 14695981039346656037ULL;
constexpr uint64_t FNV_PRIME = 1099511628211ULL;

// Avalanche mixer (MurmurHash3 / SplitMix64 finalizer) to ensure uniform bit distribution
uint64_t mixBits(uint64_t h) noexcept {
    h ^= h >> 33;
    h *= 0xff51afd7ed558ccdULL;
    h ^= h >> 33;
    h *= 0xc4ceb9fe1a85ec53ULL;
    h ^= h >> 33;
    return h;
}

} // namespace

uint64_t defaultHash64(std::string_view key) noexcept {
    uint64_t hash = FNV_OFFSET_BASIS;
    for (const char c : key) {
        hash ^= static_cast<uint8_t>(c);
        hash *= FNV_PRIME;
    }
    return mixBits(hash);
}

} // namespace ringcache
