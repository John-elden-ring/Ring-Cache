#include "ringcache/tcp_server.hpp"
#include "ringcache/protocol.hpp"
#include "ringcache/tcp_session.hpp"

#include <iostream>
#include <stdexcept>

namespace ringcache {

TcpServer::TcpServer(std::shared_ptr<CacheNode> node, ServerConfig config)
    : node_(std::move(node)),
      config_(std::move(config)) {
    if (!node_) {
        throw std::invalid_argument("TcpServer requires a non-null CacheNode");
    }
}

TcpServer::~TcpServer() {
    stop();
}

void TcpServer::start() {
    if (isRunning_.exchange(true)) {
        return;
    }

    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(config_.host), config_.port);
    acceptor_ = std::make_unique<asio::ip::tcp::acceptor>(ioContext_);
    acceptor_->open(endpoint.protocol());
    acceptor_->set_option(asio::ip::tcp::acceptor::reuse_address(true));
    acceptor_->bind(endpoint);
    acceptor_->listen();

    doAccept();

    std::size_t threadsToSpawn = config_.numThreads > 0 ? config_.numThreads : 1;
    for (std::size_t i = 1; i < threadsToSpawn; ++i) {
        workerThreads_.emplace_back([this]() {
            ioContext_.run();
        });
    }
}

void TcpServer::run() {
    start();
    ioContext_.run();
}

void TcpServer::stop() {
    if (!isRunning_.exchange(false)) {
        return;
    }

    asio::error_code ec;
    if (acceptor_ && acceptor_->is_open()) {
        acceptor_->close(ec);
    }

    ioContext_.stop();

    for (auto& thread : workerThreads_) {
        if (thread.joinable()) {
            thread.join();
        }
    }
    workerThreads_.clear();
}

uint16_t TcpServer::getBoundPort() const {
    if (acceptor_ && acceptor_->is_open()) {
        return acceptor_->local_endpoint().port();
    }
    return config_.port;
}

std::size_t TcpServer::getActiveConnections() const noexcept {
    return activeConnections_.load();
}

bool TcpServer::isRunning() const noexcept {
    return isRunning_.load();
}

std::shared_ptr<CacheNode> TcpServer::getNode() const noexcept {
    return node_;
}

void TcpServer::doAccept() {
    if (!isRunning_) {
        return;
    }

    acceptor_->async_accept([this](const asio::error_code& ec, asio::ip::tcp::socket socket) {
        if (!ec) {
            if (activeConnections_ >= config_.maxConnections) {
                // Reject connection due to limit
                Response res{StatusCode::Error, std::nullopt, "Server connection limit reached", 0};
                std::string frame = serializeResponse(res);
                asio::error_code ignoredEc;
                asio::write(socket, asio::buffer(frame), ignoredEc);
                socket.close(ignoredEc);
            } else {
                auto session = std::make_shared<TcpSession>(
                    std::move(socket),
                    node_,
                    activeConnections_
                );
                session->start();
            }
        }

        if (isRunning_) {
            doAccept();
        }
    });
}

} // namespace ringcache
