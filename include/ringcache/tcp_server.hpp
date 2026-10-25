#pragma once

#include "ringcache/cache_node.hpp"

#include <asio.hpp>

#include <atomic>
#include <cstdint>
#include <memory>
#include <string>
#include <thread>
#include <vector>

namespace ringcache {

/// @brief Server configuration options.
struct ServerConfig {
    std::string host{"127.0.0.1"};
    uint16_t port{8001};
    std::size_t numThreads{2};
    std::size_t maxConnections{1024};
};

/// @brief Multi-threaded TCP server hosting a cache node.
///
/// Binds to a local host and port, listens for incoming TCP client connections,
/// and manages asynchronous sessions serviced by a fixed worker thread pool.
class TcpServer {
public:
    explicit TcpServer(std::shared_ptr<CacheNode> node, ServerConfig config = {});
    ~TcpServer();

    TcpServer(const TcpServer&) = delete;
    TcpServer& operator=(const TcpServer&) = delete;
    TcpServer(TcpServer&&) = delete;
    TcpServer& operator=(TcpServer&&) = delete;

    /// @brief Start the server asynchronously without blocking the caller.
    void start();

    /// @brief Start the server and block the calling thread running the event loop.
    void run();

    /// @brief Stop listening, close active connections, and join worker threads.
    void stop();

    /// @brief Return the actual local port the server is bound to (useful if port 0 was passed).
    [[nodiscard]] uint16_t getBoundPort() const;

    /// @brief Return the number of currently active TCP client sessions.
    [[nodiscard]] std::size_t getActiveConnections() const noexcept;

    /// @brief Check whether the server is currently running.
    [[nodiscard]] bool isRunning() const noexcept;

    /// @brief Access the underlying cache node.
    [[nodiscard]] std::shared_ptr<CacheNode> getNode() const noexcept;

private:
    void doAccept();

    std::shared_ptr<CacheNode> node_;
    ServerConfig config_;

    asio::io_context ioContext_;
    std::unique_ptr<asio::ip::tcp::acceptor> acceptor_;

    std::atomic<bool> isRunning_{false};
    std::atomic<std::size_t> activeConnections_{0};
    std::vector<std::thread> workerThreads_;
};

} // namespace ringcache
