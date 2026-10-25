#include "ringcache/cluster_router.hpp"

#include <mutex>
#include <utility>

namespace ringcache {

ClusterRouter::ClusterRouter(std::size_t replicationFactor, std::size_t defaultVnodes)
    : replicationFactor_(replicationFactor),
      defaultVnodes_(defaultVnodes == 0 ? HashRing::DEFAULT_VNODES : defaultVnodes),
      ring_(defaultVnodes_) {}

bool ClusterRouter::addNode(const std::string& nodeId, std::size_t cacheCapacity, std::size_t vnodes) {
    if (nodeId.empty()) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    if (nodes_.find(nodeId) != nodes_.end()) {
        return false; // Duplicate node ID
    }

    if (!ring_.addNode(nodeId, vnodes)) {
        return false;
    }

    nodes_.emplace(nodeId, std::make_shared<CacheNode>(nodeId, cacheCapacity));
    return true;
}

bool ClusterRouter::removeNode(const std::string& nodeId) {
    if (nodeId.empty()) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);

    const auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) {
        return false;
    }

    ring_.removeNode(nodeId);
    nodes_.erase(it);
    return true;
}

bool ClusterRouter::hasNode(const std::string& nodeId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return nodes_.find(nodeId) != nodes_.end();
}

std::size_t ClusterRouter::nodeCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return nodes_.size();
}

std::vector<std::string> ClusterRouter::getNodeIds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(nodes_.size());
    for (const auto& [id, _] : nodes_) {
        ids.push_back(id);
    }
    return ids;
}

std::size_t ClusterRouter::getReplicationFactor() const noexcept {
    return replicationFactor_;
}

void ClusterRouter::setReplicationFactor(std::size_t r) noexcept {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    replicationFactor_ = r;
}

std::vector<std::string> ClusterRouter::getTargetNodes(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (replicationFactor_ == 0 || nodes_.empty() || key.empty()) {
        return {};
    }
    return ring_.getNodesForKey(key, replicationFactor_);
}

bool ClusterRouter::set(const std::string& key, const std::string& value) {
    return setWithDetails(key, value).success;
}

bool ClusterRouter::put(const std::string& key, const std::string& value) {
    return set(key, value);
}

WriteResult ClusterRouter::setWithDetails(const std::string& key, const std::string& value) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (replicationFactor_ == 0 || nodes_.empty() || key.empty()) {
        return WriteResult{false, 0, 0, "", {}};
    }

    const auto targetNodeIds = ring_.getNodesForKey(key, replicationFactor_);
    if (targetNodeIds.empty()) {
        return WriteResult{false, 0, 0, "", {}};
    }

    // Write to primary node first
    const auto primaryIt = nodes_.find(targetNodeIds[0]);
    if (primaryIt == nodes_.end() || !primaryIt->second->put(key, value)) {
        return WriteResult{false, 0, targetNodeIds.size(), targetNodeIds[0], {}};
    }

    std::size_t written = 1;
    std::vector<std::string> replicas;
    replicas.reserve(targetNodeIds.size() > 1 ? targetNodeIds.size() - 1 : 0);

    for (std::size_t i = 1; i < targetNodeIds.size(); ++i) {
        replicas.push_back(targetNodeIds[i]);
        const auto replicaIt = nodes_.find(targetNodeIds[i]);
        if (replicaIt != nodes_.end() && replicaIt->second->put(key, value)) {
            written++;
        }
    }

    return WriteResult{true, written, targetNodeIds.size(), targetNodeIds[0], std::move(replicas)};
}

std::optional<std::string> ClusterRouter::get(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (replicationFactor_ == 0 || nodes_.empty() || key.empty()) {
        return std::nullopt;
    }

    const auto targetNodeIds = ring_.getNodesForKey(key, replicationFactor_);
    if (targetNodeIds.empty()) {
        return std::nullopt;
    }

    // 1. Primary read
    const auto primaryIt = nodes_.find(targetNodeIds[0]);
    if (primaryIt != nodes_.end()) {
        const auto val = primaryIt->second->get(key);
        if (val.has_value()) {
            return val;
        }
    }

    // 2. Replica fallback in clockwise order
    for (std::size_t i = 1; i < targetNodeIds.size(); ++i) {
        const auto replicaIt = nodes_.find(targetNodeIds[i]);
        if (replicaIt != nodes_.end()) {
            const auto val = replicaIt->second->get(key);
            if (val.has_value()) {
                return val;
            }
        }
    }

    return std::nullopt;
}

bool ClusterRouter::del(const std::string& key) {
    std::shared_lock<std::shared_mutex> lock(mutex_);

    if (replicationFactor_ == 0 || nodes_.empty() || key.empty()) {
        return false;
    }

    const auto targetNodeIds = ring_.getNodesForKey(key, replicationFactor_);
    if (targetNodeIds.empty()) {
        return false;
    }

    bool removedAny = false;
    for (const auto& nodeId : targetNodeIds) {
        const auto it = nodes_.find(nodeId);
        if (it != nodes_.end() && it->second->remove(key)) {
            removedAny = true;
        }
    }

    return removedAny;
}

bool ClusterRouter::remove(const std::string& key) {
    return del(key);
}

std::optional<std::string> ClusterRouter::peekNodeKey(const std::string& nodeId,
                                                     const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) {
        return std::nullopt;
    }
    return it->second->peek(key);
}

bool ClusterRouter::nodeContains(const std::string& nodeId, const std::string& key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) {
        return false;
    }
    return it->second->contains(key);
}

std::optional<std::size_t> ClusterRouter::getNodeSize(const std::string& nodeId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    const auto it = nodes_.find(nodeId);
    if (it == nodes_.end()) {
        return std::nullopt;
    }
    return it->second->size();
}

void ClusterRouter::clearAllNodes() {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    for (const auto& [_, node] : nodes_) {
        node->clear();
    }
}

} // namespace ringcache
