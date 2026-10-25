#pragma once

#include "ringcache/protocol.hpp"

#include <asio.hpp>

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <optional>
#include <string>

namespace ringcache {

/// @brief Connection endpoint representing a remote cache node.
struct NodeEndpoint {
    std::string host{"127.0.0.1"};
    uint16_t port{8001};
};

/// @brief Client for communicating with a single remote cache node over TCP.
///
/// Features:
/// - Connect and operation timeouts with cancellation timer.
/// - Bounded request and response framing.
/// - Automatic reconnection upon connection drop.
/// - Thread-safe request dispatching.
class NetworkClient {
public:
    explicit NetworkClient(std::string host,
                           uint16_t port,
                           std::chrono::milliseconds timeout = std::chrono::milliseconds(2000));

    ~NetworkClient();

    NetworkClient(const NetworkClient&) = delete;
    NetworkClient& operator=(const NetworkClient&) = delete;
    NetworkClient(NetworkClient&&) = delete;
    NetworkClient& operator=(NetworkClient&&) = delete;

    /// @brief Execute a raw request over TCP with timeout and return the parsed response.
    /// @param req Request struct to serialize and send.
    /// @param errorMessage Optional pointer to string receiving error detail if failed.
    /// @return Parsed Response struct, or std::nullopt if connection or operation failed.
    std::optional<Response> execute(const Request& req, std::string* errorMessage = nullptr);

    /// @brief Store a key-value pair on the remote node.
    bool put(const std::string& key, const std::string& value, std::string* errorMessage = nullptr);

    /// @brief Retrieve a key's value from the remote node.
    std::optional<std::string> get(const std::string& key, std::string* errorMessage = nullptr);

    /// @brief Remove a key from the remote node.
    bool del(const std::string& key, std::string* errorMessage = nullptr);

    /// @brief Send a PING probe to verify node responsiveness.
    bool ping(std::string* errorMessage = nullptr);

    /// @brief Explicitly disconnect from the remote node.
    void close();

    /// @brief Check whether the client socket is currently open.
    [[nodiscard]] bool isConnected() const;

    /// @brief Get the configured endpoint host.
    [[nodiscard]] const std::string& getHost() const noexcept;

    /// @brief Get the configured endpoint port.
    [[nodiscard]] uint16_t getPort() const noexcept;

private:
    bool ensureConnected(std::string* errorMessage);

    std::string host_;
    uint16_t port_;
    std::chrono::milliseconds timeout_;

    mutable std::recursive_mutex mutex_;
    asio::io_context ioContext_;
    std::unique_ptr<asio::ip::tcp::socket> socket_;
    uint64_t nextReqId_{1};
};

} // namespace ringcache
