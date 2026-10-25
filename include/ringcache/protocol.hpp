#pragma once

#include <cstddef>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

namespace ringcache {

/// @brief Maximum allowed wire frame size in bytes (64 KB).
constexpr std::size_t MAX_FRAME_SIZE = 64 * 1024;

/// @brief Supported cache operations over TCP.
enum class OpType {
    Put,
    Get,
    Delete,
    Ping,
    Unknown
};

/// @brief Protocol response status codes.
enum class StatusCode {
    Ok,
    NotFound,
    Error,
    Malformed
};

/// @brief Convert OpType to uppercase wire string ("PUT", "GET", "DELETE", "PING", "UNKNOWN").
[[nodiscard]] std::string_view opTypeToString(OpType op) noexcept;

/// @brief Convert wire string to OpType.
[[nodiscard]] OpType stringToOpType(std::string_view str) noexcept;

/// @brief Convert StatusCode to wire string ("OK", "NOT_FOUND", "ERROR", "MALFORMED").
[[nodiscard]] std::string_view statusCodeToString(StatusCode code) noexcept;

/// @brief Convert wire string to StatusCode.
[[nodiscard]] StatusCode stringToStatusCode(std::string_view str) noexcept;

/// @brief Structured representation of a client request.
struct Request {
    OpType op{OpType::Unknown};
    std::string key;
    std::string value;
    uint64_t reqId{0};
};

/// @brief Structured representation of a server response.
struct Response {
    StatusCode status{StatusCode::Error};
    std::optional<std::string> value;
    std::string message;
    uint64_t reqId{0};
};

/// @brief Serialize a request into a newline-delimited JSON frame.
[[nodiscard]] std::string serializeRequest(const Request& req);

/// @brief Serialize a response into a newline-delimited JSON frame.
[[nodiscard]] std::string serializeResponse(const Response& res);

/// @brief Parse a single newline-delimited JSON request frame safely.
/// @param frame Input frame string (with or without trailing newline).
/// @param errorMessage Optional pointer to string receiving error description on failure.
/// @return Valid Request struct, or std::nullopt if frame is malformed or exceeds bounds.
[[nodiscard]] std::optional<Request> parseRequest(std::string_view frame,
                                                 std::string* errorMessage = nullptr);

/// @brief Parse a single newline-delimited JSON response frame safely.
/// @param frame Input frame string (with or without trailing newline).
/// @param errorMessage Optional pointer to string receiving error description on failure.
/// @return Valid Response struct, or std::nullopt if frame is malformed or exceeds bounds.
[[nodiscard]] std::optional<Response> parseResponse(std::string_view frame,
                                                   std::string* errorMessage = nullptr);

} // namespace ringcache
