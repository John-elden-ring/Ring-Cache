#pragma once

#include "ringcache/cache_node.hpp"
#include "ringcache/protocol.hpp"

#include <asio.hpp>

#include <atomic>
#include <memory>
#include <string>

namespace ringcache {

/// @brief Represents an active client TCP session on a cache node.
///
/// Reads newline-delimited frames, validates message boundaries, dispatches
/// operations to the local CacheNode, and writes responses asynchronously.
class TcpSession : public std::enable_shared_from_this<TcpSession> {
public:
    explicit TcpSession(asio::ip::tcp::socket socket,
                        std::shared_ptr<CacheNode> node,
                        std::atomic<std::size_t>& connectionCounter);

    ~TcpSession();

    TcpSession(const TcpSession&) = delete;
    TcpSession& operator=(const TcpSession&) = delete;
    TcpSession(TcpSession&&) = delete;
    TcpSession& operator=(TcpSession&&) = delete;

    /// @brief Begin asynchronous reading from the connected client socket.
    void start();

    /// @brief Close the session socket.
    void close();

private:
    void doRead();
    void processLine(std::string_view line);
    void doWrite(std::string responseFrame);

    asio::ip::tcp::socket socket_;
    std::shared_ptr<CacheNode> node_;
    std::atomic<std::size_t>& connectionCounter_;
    asio::streambuf readBuffer_;
    std::string writeBuffer_;
};

} // namespace ringcache
