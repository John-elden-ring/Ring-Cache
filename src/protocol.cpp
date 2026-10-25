#include "ringcache/protocol.hpp"

#include <nlohmann/json.hpp>

#include <algorithm>
#include <string>
#include <string_view>

namespace ringcache {

namespace {

using json = nlohmann::json;

std::string_view trimTrailingNewline(std::string_view s) noexcept {
    while (!s.empty() && (s.back() == '\n' || s.back() == '\r')) {
        s.remove_suffix(1);
    }
    return s;
}

} // namespace

std::string_view opTypeToString(OpType op) noexcept {
    switch (op) {
        case OpType::Put:
            return "PUT";
        case OpType::Get:
            return "GET";
        case OpType::Delete:
            return "DELETE";
        case OpType::Ping:
            return "PING";
        case OpType::Unknown:
        default:
            return "UNKNOWN";
    }
}

OpType stringToOpType(std::string_view str) noexcept {
    if (str == "PUT") return OpType::Put;
    if (str == "GET") return OpType::Get;
    if (str == "DELETE") return OpType::Delete;
    if (str == "PING") return OpType::Ping;
    return OpType::Unknown;
}

std::string_view statusCodeToString(StatusCode code) noexcept {
    switch (code) {
        case StatusCode::Ok:
            return "OK";
        case StatusCode::NotFound:
            return "NOT_FOUND";
        case StatusCode::Error:
            return "ERROR";
        case StatusCode::Malformed:
        default:
            return "MALFORMED";
    }
}

StatusCode stringToStatusCode(std::string_view str) noexcept {
    if (str == "OK") return StatusCode::Ok;
    if (str == "NOT_FOUND") return StatusCode::NotFound;
    if (str == "ERROR") return StatusCode::Error;
    if (str == "MALFORMED") return StatusCode::Malformed;
    return StatusCode::Malformed;
}

std::string serializeRequest(const Request& req) {
    json j;
    j["op"] = opTypeToString(req.op);
    if (req.op != OpType::Ping) {
        j["key"] = req.key;
    }
    if (req.op == OpType::Put) {
        j["val"] = req.value;
    }
    if (req.reqId != 0) {
        j["req_id"] = req.reqId;
    }
    std::string out = j.dump();
    out.push_back('\n');
    return out;
}

std::string serializeResponse(const Response& res) {
    json j;
    j["status"] = statusCodeToString(res.status);
    if (res.value.has_value()) {
        j["val"] = *res.value;
    }
    if (!res.message.empty()) {
        j["msg"] = res.message;
    }
    if (res.reqId != 0) {
        j["req_id"] = res.reqId;
    }
    std::string out = j.dump();
    out.push_back('\n');
    return out;
}

std::optional<Request> parseRequest(std::string_view frame, std::string* errorMessage) {
    if (frame.size() > MAX_FRAME_SIZE) {
        if (errorMessage) *errorMessage = "Frame size exceeds maximum allowed bound (64 KB)";
        return std::nullopt;
    }

    std::string_view trimmed = trimTrailingNewline(frame);
    if (trimmed.empty()) {
        if (errorMessage) *errorMessage = "Empty frame received";
        return std::nullopt;
    }

    json j = json::parse(trimmed.begin(), trimmed.end(), nullptr, false);
    if (j.is_discarded()) {
        if (errorMessage) *errorMessage = "Malformed JSON syntax";
        return std::nullopt;
    }

    if (!j.is_object()) {
        if (errorMessage) *errorMessage = "Request payload must be a JSON object";
        return std::nullopt;
    }

    auto opIt = j.find("op");
    if (opIt == j.end() || !opIt->is_string()) {
        if (errorMessage) *errorMessage = "Missing or invalid 'op' string field";
        return std::nullopt;
    }

    std::string opStr = opIt->get<std::string>();
    OpType op = stringToOpType(opStr);
    if (op == OpType::Unknown) {
        if (errorMessage) *errorMessage = "Unsupported or unknown operation: " + opStr;
        return std::nullopt;
    }

    Request req;
    req.op = op;

    if (auto idIt = j.find("req_id"); idIt != j.end() && idIt->is_number_unsigned()) {
        req.reqId = idIt->get<uint64_t>();
    }

    if (op == OpType::Ping) {
        return req;
    }

    auto keyIt = j.find("key");
    if (keyIt == j.end() || !keyIt->is_string()) {
        if (errorMessage) *errorMessage = "Missing or invalid 'key' field for operation " + opStr;
        return std::nullopt;
    }
    req.key = keyIt->get<std::string>();

    if (op == OpType::Put) {
        auto valIt = j.find("val");
        if (valIt == j.end()) {
            valIt = j.find("value");
        }
        if (valIt == j.end() || !valIt->is_string()) {
            if (errorMessage) *errorMessage = "Missing or invalid 'val'/'value' field for PUT";
            return std::nullopt;
        }
        req.value = valIt->get<std::string>();
    }

    return req;
}

std::optional<Response> parseResponse(std::string_view frame, std::string* errorMessage) {
    if (frame.size() > MAX_FRAME_SIZE) {
        if (errorMessage) *errorMessage = "Frame size exceeds maximum allowed bound (64 KB)";
        return std::nullopt;
    }

    std::string_view trimmed = trimTrailingNewline(frame);
    if (trimmed.empty()) {
        if (errorMessage) *errorMessage = "Empty frame received";
        return std::nullopt;
    }

    json j = json::parse(trimmed.begin(), trimmed.end(), nullptr, false);
    if (j.is_discarded()) {
        if (errorMessage) *errorMessage = "Malformed JSON syntax";
        return std::nullopt;
    }

    if (!j.is_object()) {
        if (errorMessage) *errorMessage = "Response payload must be a JSON object";
        return std::nullopt;
    }

    auto statusIt = j.find("status");
    if (statusIt == j.end() || !statusIt->is_string()) {
        if (errorMessage) *errorMessage = "Missing or invalid 'status' field";
        return std::nullopt;
    }

    Response res;
    res.status = stringToStatusCode(statusIt->get<std::string>());

    if (auto idIt = j.find("req_id"); idIt != j.end() && idIt->is_number_unsigned()) {
        res.reqId = idIt->get<uint64_t>();
    }

    auto valIt = j.find("val");
    if (valIt == j.end()) {
        valIt = j.find("value");
    }
    if (valIt != j.end() && valIt->is_string()) {
        res.value = valIt->get<std::string>();
    }

    auto msgIt = j.find("msg");
    if (msgIt == j.end()) {
        msgIt = j.find("message");
    }
    if (msgIt != j.end() && msgIt->is_string()) {
        res.message = msgIt->get<std::string>();
    }

    return res;
}

} // namespace ringcache
