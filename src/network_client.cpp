#include "ringcache/network_client.hpp"

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <sys/select.h>
#include <sys/socket.h>
#endif

#include <iostream>
#include <istream>

namespace ringcache {

NetworkClient::NetworkClient(std::string host,
                             uint16_t port,
                             std::chrono::milliseconds timeout)
    : host_(std::move(host)),
      port_(port),
      timeout_(timeout) {}

NetworkClient::~NetworkClient() {
    close();
}

void NetworkClient::close() {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    if (socket_ && socket_->is_open()) {
        asio::error_code ec;
        socket_->shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_->close(ec);
    }
    socket_.reset();
}

bool NetworkClient::isConnected() const {
    std::lock_guard<std::recursive_mutex> lock(mutex_);
    return socket_ && socket_->is_open();
}

const std::string& NetworkClient::getHost() const noexcept {
    return host_;
}

uint16_t NetworkClient::getPort() const noexcept {
    return port_;
}

bool NetworkClient::ensureConnected(std::string* errorMessage) {
    if (socket_ && socket_->is_open()) {
        return true;
    }

    socket_ = std::make_unique<asio::ip::tcp::socket>(ioContext_);

    asio::ip::tcp::endpoint endpoint(asio::ip::make_address(host_), port_);
    asio::error_code ec;
    socket_->open(endpoint.protocol(), ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Failed to open socket: " + ec.message();
        }
        close();
        return false;
    }

    socket_->non_blocking(true, ec);
    socket_->connect(endpoint, ec);

    if (ec && ec != asio::error::would_block && ec != asio::error::in_progress) {
        if (errorMessage) {
            *errorMessage = "Connect to " + host_ + ":" + std::to_string(port_) + " failed: " + ec.message();
        }
        close();
        return false;
    }

    if (ec) {
        // Wait for connect completion using select()
        auto nativeFd = socket_->native_handle();
        fd_set writeSet, errSet;
        FD_ZERO(&writeSet);
        FD_ZERO(&errSet);
        FD_SET(nativeFd, &writeSet);
        FD_SET(nativeFd, &errSet);

        timeval tv;
        tv.tv_sec = static_cast<long>(timeout_.count() / 1000);
        tv.tv_usec = static_cast<long>((timeout_.count() % 1000) * 1000);

        int sel = ::select(static_cast<int>(nativeFd + 1), nullptr, &writeSet, &errSet, &tv);
        if (sel <= 0) {
            if (errorMessage) {
                *errorMessage = (sel == 0) ? "Connection timed out" : "Select failed";
            }
            close();
            return false;
        }

        if (FD_ISSET(nativeFd, &errSet)) {
            if (errorMessage) {
                *errorMessage = "Connection error on socket";
            }
            close();
            return false;
        }

        int socketError = 0;
#ifdef _WIN32
        int optLen = sizeof(socketError);
        ::getsockopt(nativeFd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&socketError), &optLen);
#else
        socklen_t optLen = sizeof(socketError);
        ::getsockopt(nativeFd, SOL_SOCKET, SO_ERROR, &socketError, &optLen);
#endif
        if (socketError != 0) {
            if (errorMessage) {
                *errorMessage = "Connection refused or failed on " + host_ + ":" + std::to_string(port_);
            }
            close();
            return false;
        }
    }

    socket_->non_blocking(false, ec);
    return true;
}

std::optional<Response> NetworkClient::execute(const Request& req, std::string* errorMessage) {
    std::lock_guard<std::recursive_mutex> lock(mutex_);

    if (!ensureConnected(errorMessage)) {
        return std::nullopt;
    }

    std::string frame = serializeRequest(req);

    asio::error_code ec;
    asio::write(*socket_, asio::buffer(frame), ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Socket write failed: " + ec.message();
        }
        close();
        return std::nullopt;
    }

    // Wait for read data using select()
    auto nativeFd = socket_->native_handle();
    fd_set readSet, errSet;
    FD_ZERO(&readSet);
    FD_ZERO(&errSet);
    FD_SET(nativeFd, &readSet);
    FD_SET(nativeFd, &errSet);

    timeval tv;
    tv.tv_sec = static_cast<long>(timeout_.count() / 1000);
    tv.tv_usec = static_cast<long>((timeout_.count() % 1000) * 1000);

    int sel = ::select(static_cast<int>(nativeFd + 1), &readSet, nullptr, &errSet, &tv);
    if (sel <= 0) {
        if (errorMessage) {
            *errorMessage = (sel == 0) ? "Read timed out" : "Select error during read";
        }
        close();
        return std::nullopt;
    }

    if (FD_ISSET(nativeFd, &errSet)) {
        if (errorMessage) {
            *errorMessage = "Socket error/disconnect signaled on read";
        }
        close();
        return std::nullopt;
    }

    asio::streambuf readBuffer;
    asio::read_until(*socket_, readBuffer, '\n', ec);
    if (ec) {
        if (errorMessage) {
            *errorMessage = "Socket read failed: " + ec.message();
        }
        close();
        return std::nullopt;
    }

    std::istream stream(&readBuffer);
    std::string line;
    if (!std::getline(stream, line)) {
        if (errorMessage) {
            *errorMessage = "Failed to extract response line from buffer";
        }
        close();
        return std::nullopt;
    }

    return parseResponse(line, errorMessage);
}

bool NetworkClient::put(const std::string& key, const std::string& value, std::string* errorMessage) {
    Request req;
    req.op = OpType::Put;
    req.key = key;
    req.value = value;
    req.reqId = nextReqId_++;

    auto res = execute(req, errorMessage);
    if (!res) {
        return false;
    }
    if (res->status == StatusCode::Ok) {
        return true;
    }
    if (errorMessage && !res->message.empty()) {
        *errorMessage = res->message;
    }
    return false;
}

std::optional<std::string> NetworkClient::get(const std::string& key, std::string* errorMessage) {
    Request req;
    req.op = OpType::Get;
    req.key = key;
    req.reqId = nextReqId_++;

    auto res = execute(req, errorMessage);
    if (!res) {
        return std::nullopt;
    }
    if (res->status == StatusCode::Ok) {
        return res->value.value_or("");
    }
    return std::nullopt;
}

bool NetworkClient::del(const std::string& key, std::string* errorMessage) {
    Request req;
    req.op = OpType::Delete;
    req.key = key;
    req.reqId = nextReqId_++;

    auto res = execute(req, errorMessage);
    if (!res) {
        return false;
    }
    return (res->status == StatusCode::Ok);
}

bool NetworkClient::ping(std::string* errorMessage) {
    Request req;
    req.op = OpType::Ping;
    req.reqId = nextReqId_++;

    auto res = execute(req, errorMessage);
    return res && (res->status == StatusCode::Ok);
}

} // namespace ringcache
