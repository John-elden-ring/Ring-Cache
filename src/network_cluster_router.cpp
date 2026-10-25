#include "ringcache/network_cluster_router.hpp"

namespace ringcache {

NetworkClusterRouter::NetworkClusterRouter(std::size_t replicationFactor,
                                           std::size_t defaultVnodes,
                                           std::chrono::milliseconds timeout)
    : replicationFactor_(replicationFactor),
      defaultVnodes_(defaultVnodes),
      timeout_(timeout) {}

bool NetworkClusterRouter::addNode(const std::string& nodeId,
                                   const std::string& host,
                                   uint16_t port,
                                   std::size_t vnodes) {
    if (nodeId.empty() || host.empty() || port == 0) {
        return false;
    }

    std::unique_lock<std::shared_mutex> lock(mutex_);
    if (endpoints_.find(nodeId) != endpoints_.end()) {
        return false;
    }

    const std::size_t effectiveVnodes = (vnodes == 0) ? defaultVnodes_ : vnodes;
    if (!ring_.addNode(nodeId, effectiveVnodes)) {
        return false;
    }

    endpoints_[nodeId] = NodeEndpoint{host, port};
    // Eagerly or lazily prepare client
    clients_[nodeId] = std::make_shared<NetworkClient>(host, port, timeout_);
    return true;
}

bool NetworkClusterRouter::removeNode(const std::string& nodeId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = endpoints_.find(nodeId);
    if (it == endpoints_.end()) {
        return false;
    }

    ring_.removeNode(nodeId);
    endpoints_.erase(it);

    if (auto clientIt = clients_.find(nodeId); clientIt != clients_.end()) {
        clientIt->second->close();
        clients_.erase(clientIt);
    }
    return true;
}

bool NetworkClusterRouter::hasNode(const std::string& nodeId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return endpoints_.find(nodeId) != endpoints_.end();
}

std::size_t NetworkClusterRouter::nodeCount() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    return endpoints_.size();
}

std::vector<std::string> NetworkClusterRouter::getNodeIds() const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    std::vector<std::string> ids;
    ids.reserve(endpoints_.size());
    for (const auto& [id, _] : endpoints_) {
        ids.push_back(id);
    }
    return ids;
}

std::size_t NetworkClusterRouter::getReplicationFactor() const noexcept {
    return replicationFactor_;
}

void NetworkClusterRouter::setReplicationFactor(std::size_t r) noexcept {
    replicationFactor_ = r;
}

std::vector<std::string> NetworkClusterRouter::getTargetNodes(std::string_view key) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    if (ring_.empty() || replicationFactor_ == 0 || key.empty()) {
        return {};
    }
    return ring_.getNodesForKey(key, replicationFactor_);
}

std::shared_ptr<NetworkClient> NetworkClusterRouter::getOrCreateClient(const std::string& nodeId) {
    std::unique_lock<std::shared_mutex> lock(mutex_);
    auto it = clients_.find(nodeId);
    if (it != clients_.end()) {
        return it->second;
    }

    auto epIt = endpoints_.find(nodeId);
    if (epIt == endpoints_.end()) {
        return nullptr;
    }

    auto client = std::make_shared<NetworkClient>(epIt->second.host, epIt->second.port, timeout_);
    clients_[nodeId] = client;
    return client;
}

bool NetworkClusterRouter::put(const std::string& key, const std::string& value) {
    return putWithDetails(key, value).success;
}

bool NetworkClusterRouter::set(const std::string& key, const std::string& value) {
    return put(key, value);
}

WriteResult NetworkClusterRouter::putWithDetails(const std::string& key, const std::string& value) {
    std::vector<std::string> targets = getTargetNodes(key);
    if (targets.empty()) {
        return {};
    }

    std::vector<std::shared_ptr<NetworkClient>> clientList;
    clientList.reserve(targets.size());
    for (const auto& node : targets) {
        clientList.push_back(getOrCreateClient(node));
    }

    WriteResult result;
    result.targetCount = targets.size();
    result.primaryNode = targets.front();

    // Primary write
    auto primaryClient = clientList.front();
    if (!primaryClient || !primaryClient->put(key, value)) {
        result.success = false;
        return result;
    }

    result.success = true;
    result.writtenCount = 1;

    // Propagate to replicas
    for (std::size_t i = 1; i < targets.size(); ++i) {
        auto replicaClient = clientList[i];
        if (replicaClient && replicaClient->put(key, value)) {
            result.writtenCount++;
            result.replicaNodes.push_back(targets[i]);
        }
    }

    return result;
}

std::optional<std::string> NetworkClusterRouter::get(const std::string& key) {
    std::vector<std::string> targets = getTargetNodes(key);
    if (targets.empty()) {
        return std::nullopt;
    }

    // Try primary first
    auto primaryClient = getOrCreateClient(targets.front());
    if (primaryClient) {
        auto val = primaryClient->get(key);
        if (val.has_value()) {
            return val;
        }
    }

    // Fall back to clockwise replicas
    for (std::size_t i = 1; i < targets.size(); ++i) {
        auto replicaClient = getOrCreateClient(targets[i]);
        if (replicaClient) {
            auto val = replicaClient->get(key);
            if (val.has_value()) {
                return val;
            }
        }
    }

    return std::nullopt;
}

bool NetworkClusterRouter::del(const std::string& key) {
    std::vector<std::string> targets = getTargetNodes(key);
    if (targets.empty()) {
        return false;
    }

    bool anyDeleted = false;
    for (const auto& node : targets) {
        auto client = getOrCreateClient(node);
        if (client && client->del(key)) {
            anyDeleted = true;
        }
    }

    return anyDeleted;
}

bool NetworkClusterRouter::remove(const std::string& key) {
    return del(key);
}

bool NetworkClusterRouter::pingNode(const std::string& nodeId) {
    auto client = getOrCreateClient(nodeId);
    if (!client) {
        return false;
    }
    return client->ping();
}

std::optional<NodeEndpoint> NetworkClusterRouter::getNodeEndpoint(const std::string& nodeId) const {
    std::shared_lock<std::shared_mutex> lock(mutex_);
    auto it = endpoints_.find(nodeId);
    if (it != endpoints_.end()) {
        return it->second;
    }
    return std::nullopt;
}

} // namespace ringcache
