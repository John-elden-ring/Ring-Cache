#pragma once

#include <chrono>
#include <cstddef>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace ringcache {

/// @brief Explicit error categories for client operations.
enum class ErrorCode {
    Success = 0,
    InvalidConfiguration,
    ConnectionFailed,
    Timeout,
    KeyNotFound,
    ServerLimitReached,
    ClusterUnavailable,
    ProtocolError,
    OperationFailed
};

/// @brief Detailed result struct returned or populated upon operation failure.
struct ClientError {
    ErrorCode code{ErrorCode::Success};
    std::string message;

    [[nodiscard]] bool ok() const noexcept { return code == ErrorCode::Success; }
    [[nodiscard]] explicit operator bool() const noexcept { return ok(); }
};

/// @brief Configuration options for instantiating a RingCache cluster client.
struct ClientConfig {
    /// Comma-separated list of peer endpoints (e.g., "node-1=127.0.0.1:8001,node-2=127.0.0.1:8002").
    std::string peers;

    /// Successor replication factor (default: 1).
    std::size_t replicationFactor{1};

    /// Virtual nodes per physical node on the consistent hash ring (default: 150).
    std::size_t virtualNodes{150};

    /// Per-operation socket connect, read, and write timeout (default: 2000 ms).
    std::chrono::milliseconds timeout{2000};
};

/// @brief Metrics returned by detailed write operations.
struct WriteMetrics {
    bool success{false};
    std::size_t targetCount{0};
    std::size_t writtenCount{0};
    std::string primaryNode;
    std::vector<std::string> replicaNodes;
};

/// @brief Thread-safe client for connecting to a RingCache distributed cluster.
///
/// Ownership & Lifetimes:
/// - Manages internal connection pools via RAII. Cleanly closes sockets on destruction.
/// - Move-constructible and move-assignable; non-copyable.
///
/// Concurrency:
/// - All member methods (put, get, del, ping) are thread-safe and can be safely invoked
///   concurrently across multiple application threads.
class Client {
public:
    /// @brief Construct a client using a configuration struct.
    /// @throws std::invalid_argument if peer configuration is empty or invalid.
    explicit Client(ClientConfig config);

    /// @brief Convenience constructor parsing comma-separated peers.
    /// @throws std::invalid_argument if peers argument is empty or invalid.
    explicit Client(std::string_view peers, std::size_t replicationFactor = 1);

    ~Client();

    Client(const Client&) = delete;
    Client& operator=(const Client&) = delete;
    Client(Client&&) noexcept;
    Client& operator=(Client&&) noexcept;

    /// @brief Store a key-value pair across primary and replica nodes over TCP.
    /// @param key Unique key identifier.
    /// @param value Value payload to store.
    /// @param error Optional pointer to ClientError receiving failure details.
    /// @return true if successfully written to the primary node; false otherwise.
    bool put(std::string_view key, std::string_view value, ClientError* error = nullptr);

    /// @brief Store a key-value pair and retrieve detailed write distribution metrics.
    /// @param key Unique key identifier.
    /// @param value Value payload to store.
    /// @param error Optional pointer to ClientError receiving failure details.
    /// @return WriteMetrics detailing targeted nodes, written count, and primary/replicas.
    WriteMetrics putWithMetrics(std::string_view key, std::string_view value, ClientError* error = nullptr);

    /// @brief Retrieve a key's value (primary-first with replica fallback).
    /// @param key Unique key identifier.
    /// @param error Optional pointer to ClientError receiving failure details.
    /// @return Cached value, or std::nullopt if key is absent or unreachable.
    [[nodiscard]] std::optional<std::string> get(std::string_view key, ClientError* error = nullptr);

    /// @brief Delete a key from primary and replica nodes.
    /// @param key Unique key identifier.
    /// @param error Optional pointer to ClientError receiving failure details.
    /// @return true if deleted from at least one node; false if key did not exist or failed.
    bool del(std::string_view key, ClientError* error = nullptr);

    /// @brief Probe health of a specific node by ID.
    bool ping(std::string_view nodeId, ClientError* error = nullptr);

    /// @brief Probe health of all registered cluster nodes.
    /// @return true if all nodes answer; false if any node fails to respond.
    bool pingAll(ClientError* error = nullptr);

    /// @brief Return total number of configured physical nodes.
    [[nodiscard]] std::size_t nodeCount() const noexcept;

    /// @brief Return the configured replication factor.
    [[nodiscard]] std::size_t getReplicationFactor() const noexcept;

private:
    class Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace ringcache
