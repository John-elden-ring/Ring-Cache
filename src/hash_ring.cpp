#include "ringcache/hash_ring.hpp"

#include <mutex>
#include <utility>

namespace ringcache {

HashRing::HashRing(std::size_t defaultVnodes, HashFunction hashFn)
    : defaultVnodes_(defaultVnodes == 0 ? DEFAULT_VNODES : defaultVnodes),
      hashFn_(hashFn ? std::move(hashFn) : defaultHash64) {}

bool HashRing::addNode(const std::string& nodeId, std::size_t vnodes) {
    if (nodeId.empty()) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (nodeTokens_.find(nodeId) != nodeTokens_.end()) {
        return false; // Node already registered
    }

    const std::size_t count = (vnodes == 0) ? defaultVnodes_ : vnodes;
    std::vector<uint64_t> tokens;
    tokens.reserve(count);

    for (std::size_t i = 0; i < count; ++i) {
        const std::string tokenKey = nodeId + '#' + std::to_string(i);
        uint64_t token = hashFn_(tokenKey);

        // Linear probe in the rare event of a 64-bit token collision
        while (ring_.find(token) != ring_.end()) {
            ++token;
        }

        ring_.emplace(token, nodeId);
        tokens.push_back(token);
    }

    nodeTokens_.emplace(nodeId, std::move(tokens));
    return true;
}

bool HashRing::removeNode(const std::string& nodeId) {
    if (nodeId.empty()) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    const auto it = nodeTokens_.find(nodeId);
    if (it == nodeTokens_.end()) {
        return false; // Node not found
    }

    for (const uint64_t token : it->second) {
        ring_.erase(token);
    }

    nodeTokens_.erase(it);
    return true;
}

bool HashRing::hasNode(const std::string& nodeId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return nodeTokens_.find(nodeId) != nodeTokens_.end();
}

std::optional<std::string> HashRing::getNode(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (ring_.empty() || key.empty()) {
        return std::nullopt;
    }

    const uint64_t hash = hashFn_(key);

    // Find the first token >= key hash
    auto it = ring_.lower_bound(hash);

    // If beyond the highest token, wrap around to the first token
    if (it == ring_.end()) {
        it = ring_.begin();
    }

    return it->second;
}

std::size_t HashRing::nodeCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return nodeTokens_.size();
}

std::size_t HashRing::ringSize() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ring_.size();
}

bool HashRing::empty() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return ring_.empty();
}

std::vector<std::string> HashRing::getNodes() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> nodes;
    nodes.reserve(nodeTokens_.size());
    for (const auto& [nodeId, _] : nodeTokens_) {
        nodes.push_back(nodeId);
    }
    return nodes;
}

void HashRing::clear() {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    ring_.clear();
    nodeTokens_.clear();
}

} // namespace ringcache
