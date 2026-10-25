#include "ringcache/tcp_session.hpp"

#include <iostream>
#include <istream>

namespace ringcache {

TcpSession::TcpSession(asio::ip::tcp::socket socket,
                       std::shared_ptr<CacheNode> node,
                       std::atomic<std::size_t>& connectionCounter)
    : socket_(std::move(socket)),
      node_(std::move(node)),
      connectionCounter_(connectionCounter) {
    ++connectionCounter_;
}

TcpSession::~TcpSession() {
    --connectionCounter_;
    close();
}

void TcpSession::start() {
    doRead();
}

void TcpSession::close() {
    asio::error_code ec;
    if (socket_.is_open()) {
        socket_.shutdown(asio::ip::tcp::socket::shutdown_both, ec);
        socket_.close(ec);
    }
}

void TcpSession::doRead() {
    auto self = shared_from_this();
    asio::async_read_until(
        socket_,
        readBuffer_,
        '\n',
        [this, self](const asio::error_code& ec, std::size_t /*bytesTransferred*/) {
            if (ec) {
                // Connection closed or reset
                return;
            }

            if (readBuffer_.size() > MAX_FRAME_SIZE) {
                Response res{StatusCode::Malformed, std::nullopt, "Frame exceeds 64 KB limit", 0};
                doWrite(serializeResponse(res));
                return;
            }

            std::istream stream(&readBuffer_);
            std::string line;
            if (std::getline(stream, line)) {
                processLine(line);
            }
        });
}

void TcpSession::processLine(std::string_view line) {
    std::string err;
    auto req = parseRequest(line, &err);

    if (!req) {
        Response res{StatusCode::Malformed, std::nullopt, err.empty() ? "Malformed request" : err, 0};
        doWrite(serializeResponse(res));
        return;
    }

    Response res;
    res.reqId = req->reqId;

    switch (req->op) {
        case OpType::Put: {
            bool ok = node_->put(req->key, req->value);
            res.status = ok ? StatusCode::Ok : StatusCode::Error;
            if (!ok) {
                res.message = "Key insertion rejected by cache engine (e.g. capacity zero)";
            }
            break;
        }
        case OpType::Get: {
            auto val = node_->get(req->key);
            if (val.has_value()) {
                res.status = StatusCode::Ok;
                res.value = *val;
            } else {
                res.status = StatusCode::NotFound;
                res.message = "Key not found";
            }
            break;
        }
        case OpType::Delete: {
            bool removed = node_->remove(req->key);
            res.status = removed ? StatusCode::Ok : StatusCode::NotFound;
            if (!removed) {
                res.message = "Key not found";
            }
            break;
        }
        case OpType::Ping: {
            res.status = StatusCode::Ok;
            res.message = "PONG";
            break;
        }
        case OpType::Unknown:
        default: {
            res.status = StatusCode::Malformed;
            res.message = "Unsupported operation";
            break;
        }
    }

    doWrite(serializeResponse(res));
}

void TcpSession::doWrite(std::string responseFrame) {
    writeBuffer_ = std::move(responseFrame);
    auto self = shared_from_this();
    asio::async_write(
        socket_,
        asio::buffer(writeBuffer_),
        [this, self](const asio::error_code& ec, std::size_t /*bytesWritten*/) {
            if (!ec) {
                doRead();
            }
        });
}

} // namespace ringcache
