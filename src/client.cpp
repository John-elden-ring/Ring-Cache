#include "ringcache/client.hpp"
#include "ringcache/network_cluster_router.hpp"

#include <sstream>
#include <stdexcept>
#include <string>
#include <utility>

namespace ringcache {

namespace {

bool parseEndpoint(const std::string& peerStr, std::string& nodeId, std::string& host, uint16_t& port) {
    auto eqPos = peerStr.find('=');
    if (eqPos == std::string::npos) return false;
    nodeId = peerStr.substr(0, eqPos);

    auto colonPos = peerStr.find(':', eqPos + 1);
    if (colonPos == std::string::npos) return false;
    host = peerStr.substr(eqPos + 1, colonPos - (eqPos + 1));
    try {
        port = static_cast<uint16_t>(std::stoul(peerStr.substr(colonPos + 1)));
        return (port > 0);
    } catch (...) {
        return false;
    }
}

void setErr(ClientError* err, ErrorCode code, std::string msg) {
    if (err) {
        err->code = code;
        err->message = std::move(msg);
    }
}

} // namespace

class Client::Impl {
public:
    explicit Impl(ClientConfig config)
        : config_(std::move(config)),
          router_(config_.replicationFactor,
                  config_.virtualNodes,
                  config_.timeout) {
        if (config_.peers.empty()) {
            throw std::invalid_argument("Client configuration requires non-empty peers list");
        }

        std::stringstream ss(config_.peers);
        std::string item;
        std::size_t addedCount = 0;

        while (std::getline(ss, item, ',')) {
            if (item.empty()) continue;
            std::string nodeId, host;
            uint16_t port{0};
            if (parseEndpoint(item, nodeId, host, port)) {
                if (router_.addNode(nodeId, host, port, config_.virtualNodes)) {
                    addedCount++;
                }
            }
        }

        if (addedCount == 0) {
            throw std::invalid_argument("Failed to parse any valid node endpoints from peers: " + config_.peers);
        }
    }

    bool put(std::string_view key, std::string_view value, ClientError* error) {
        if (key.empty()) {
            setErr(error, ErrorCode::InvalidConfiguration, "Key cannot be empty");
            return false;
        }

        auto res = router_.putWithDetails(std::string(key), std::string(value));
        if (res.success) {
            setErr(error, ErrorCode::Success, "");
            return true;
        }

        setErr(error, ErrorCode::OperationFailed, "Failed to write key '" + std::string(key) + "' to primary node");
        return false;
    }

    WriteMetrics putWithMetrics(std::string_view key, std::string_view value, ClientError* error) {
        WriteMetrics metrics;
        if (key.empty()) {
            setErr(error, ErrorCode::InvalidConfiguration, "Key cannot be empty");
            return metrics;
        }

        auto res = router_.putWithDetails(std::string(key), std::string(value));
        metrics.success = res.success;
        metrics.targetCount = res.targetCount;
        metrics.writtenCount = res.writtenCount;
        metrics.primaryNode = res.primaryNode;
        metrics.replicaNodes = res.replicaNodes;

        if (res.success) {
            setErr(error, ErrorCode::Success, "");
        } else {
            setErr(error, ErrorCode::OperationFailed, "Write operation failed on key: " + std::string(key));
        }
        return metrics;
    }

    std::optional<std::string> get(std::string_view key, ClientError* error) {
        if (key.empty()) {
            setErr(error, ErrorCode::InvalidConfiguration, "Key cannot be empty");
            return std::nullopt;
        }

        auto val = router_.get(std::string(key));
        if (val.has_value()) {
            setErr(error, ErrorCode::Success, "");
            return val;
        }

        setErr(error, ErrorCode::KeyNotFound, "Key not found or cluster unreachable: " + std::string(key));
        return std::nullopt;
    }

    bool del(std::string_view key, ClientError* error) {
        if (key.empty()) {
            setErr(error, ErrorCode::InvalidConfiguration, "Key cannot be empty");
            return false;
        }

        bool ok = router_.del(std::string(key));
        if (ok) {
            setErr(error, ErrorCode::Success, "");
            return true;
        }

        setErr(error, ErrorCode::KeyNotFound, "Key not found on any targeted node: " + std::string(key));
        return false;
    }

    bool ping(std::string_view nodeId, ClientError* error) {
        bool ok = router_.pingNode(std::string(nodeId));
        if (ok) {
            setErr(error, ErrorCode::Success, "");
            return true;
        }

        setErr(error, ErrorCode::ConnectionFailed, "Node unreachable or failed to respond: " + std::string(nodeId));
        return false;
    }

    bool pingAll(ClientError* error) {
        auto nodeIds = router_.getNodeIds();
        if (nodeIds.empty()) {
            setErr(error, ErrorCode::ClusterUnavailable, "No nodes configured in cluster");
            return false;
        }

        for (const auto& nid : nodeIds) {
            if (!router_.pingNode(nid)) {
                setErr(error, ErrorCode::ConnectionFailed, "Node failed health probe: " + nid);
                return false;
            }
        }

        setErr(error, ErrorCode::Success, "");
        return true;
    }

    std::size_t nodeCount() const noexcept {
        return router_.nodeCount();
    }

    std::size_t getReplicationFactor() const noexcept {
        return router_.getReplicationFactor();
    }

private:
    ClientConfig config_;
    NetworkClusterRouter router_;
};

Client::Client(ClientConfig config)
    : impl_(std::make_unique<Impl>(std::move(config))) {}

Client::Client(std::string_view peers, std::size_t replicationFactor) {
    ClientConfig cfg;
    cfg.peers = std::string(peers);
    cfg.replicationFactor = replicationFactor;
    impl_ = std::make_unique<Impl>(std::move(cfg));
}

Client::~Client() = default;

Client::Client(Client&&) noexcept = default;
Client& Client::operator=(Client&&) noexcept = default;

bool Client::put(std::string_view key, std::string_view value, ClientError* error) {
    return impl_->put(key, value, error);
}

WriteMetrics Client::putWithMetrics(std::string_view key, std::string_view value, ClientError* error) {
    return impl_->putWithMetrics(key, value, error);
}

std::optional<std::string> Client::get(std::string_view key, ClientError* error) {
    return impl_->get(key, error);
}

bool Client::del(std::string_view key, ClientError* error) {
    return impl_->del(key, error);
}

bool Client::ping(std::string_view nodeId, ClientError* error) {
    return impl_->ping(nodeId, error);
}

bool Client::pingAll(ClientError* error) {
    return impl_->pingAll(error);
}

std::size_t Client::nodeCount() const noexcept {
    return impl_->nodeCount();
}

std::size_t Client::getReplicationFactor() const noexcept {
    return impl_->getReplicationFactor();
}

} // namespace ringcache
