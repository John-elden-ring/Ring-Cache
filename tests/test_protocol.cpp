#include "ringcache/protocol.hpp"
#include "test_common.hpp"

#include <string>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getProtocolTests() {
    return {
        {"Protocol: Request Serialization and Parsing", []() {
            // PUT
            Request putReq{OpType::Put, "user:1", "Alice", 101};
            std::string serializedPut = serializeRequest(putReq);
            std::string err;
            auto parsedPut = parseRequest(serializedPut, &err);
            RC_TEST_ASSERT(parsedPut.has_value(), "Should parse valid PUT request: " + err);
            RC_TEST_ASSERT(parsedPut->op == OpType::Put, "Op should be PUT");
            RC_TEST_ASSERT(parsedPut->key == "user:1", "Key should be user:1");
            RC_TEST_ASSERT(parsedPut->value == "Alice", "Value should be Alice");
            RC_TEST_ASSERT(parsedPut->reqId == 101, "ReqId should be 101");

            // GET
            Request getReq{OpType::Get, "user:1", "", 102};
            std::string serializedGet = serializeRequest(getReq);
            auto parsedGet = parseRequest(serializedGet);
            RC_TEST_ASSERT(parsedGet.has_value(), "Should parse valid GET request");
            RC_TEST_ASSERT(parsedGet->op == OpType::Get, "Op should be GET");
            RC_TEST_ASSERT(parsedGet->key == "user:1", "Key should be user:1");
            RC_TEST_ASSERT(parsedGet->reqId == 102, "ReqId should be 102");

            // DELETE
            Request delReq{OpType::Delete, "user:1", "", 103};
            std::string serializedDel = serializeRequest(delReq);
            auto parsedDel = parseRequest(serializedDel);
            RC_TEST_ASSERT(parsedDel.has_value(), "Should parse valid DELETE request");
            RC_TEST_ASSERT(parsedDel->op == OpType::Delete, "Op should be DELETE");
            RC_TEST_ASSERT(parsedDel->key == "user:1", "Key should be user:1");

            // PING
            Request pingReq{OpType::Ping, "", "", 104};
            std::string serializedPing = serializeRequest(pingReq);
            auto parsedPing = parseRequest(serializedPing);
            RC_TEST_ASSERT(parsedPing.has_value(), "Should parse valid PING request");
            RC_TEST_ASSERT(parsedPing->op == OpType::Ping, "Op should be PING");
            RC_TEST_ASSERT(parsedPing->reqId == 104, "ReqId should be 104");
        }},

        {"Protocol: Response Serialization and Parsing", []() {
            // OK with value
            Response resOk{StatusCode::Ok, "payload_data", "", 42};
            std::string sOk = serializeResponse(resOk);
            std::string err;
            auto pOk = parseResponse(sOk, &err);
            RC_TEST_ASSERT(pOk.has_value(), "Should parse OK response: " + err);
            RC_TEST_ASSERT(pOk->status == StatusCode::Ok, "Status should be Ok");
            RC_TEST_ASSERT(pOk->value.has_value() && *pOk->value == "payload_data", "Value matches");
            RC_TEST_ASSERT(pOk->reqId == 42, "ReqId should be 42");

            // NOT_FOUND with message
            Response resNotFound{StatusCode::NotFound, std::nullopt, "Key absent", 43};
            std::string sNf = serializeResponse(resNotFound);
            auto pNf = parseResponse(sNf);
            RC_TEST_ASSERT(pNf.has_value(), "Should parse NOT_FOUND response");
            RC_TEST_ASSERT(pNf->status == StatusCode::NotFound, "Status should be NotFound");
            RC_TEST_ASSERT(!pNf->value.has_value(), "Should have no value");
            RC_TEST_ASSERT(pNf->message == "Key absent", "Message matches");

            // ERROR
            Response resErr{StatusCode::Error, std::nullopt, "Disk full", 44};
            auto pErr = parseResponse(serializeResponse(resErr));
            RC_TEST_ASSERT(pErr.has_value(), "Should parse ERROR response");
            RC_TEST_ASSERT(pErr->status == StatusCode::Error, "Status should be Error");

            // MALFORMED
            Response resMal{StatusCode::Malformed, std::nullopt, "Bad syntax", 0};
            auto pMal = parseResponse(serializeResponse(resMal));
            RC_TEST_ASSERT(pMal.has_value(), "Should parse MALFORMED response");
            RC_TEST_ASSERT(pMal->status == StatusCode::Malformed, "Status should be Malformed");
        }},

        {"Protocol: Alternative Field Names and CRLF", []() {
            // "value" instead of "val"
            std::string jsonStr = "{\"op\":\"PUT\",\"key\":\"k1\",\"value\":\"v1\",\"req_id\":99}\r\n";
            auto parsed = parseRequest(jsonStr);
            RC_TEST_ASSERT(parsed.has_value(), "Should parse 'value' field");
            RC_TEST_ASSERT(parsed->value == "v1", "Value should be v1");
            RC_TEST_ASSERT(parsed->reqId == 99, "ReqId should be 99");

            // "message" instead of "msg"
            std::string resStr = "{\"status\":\"ERROR\",\"message\":\"Something broke\"}\n";
            auto resParsed = parseResponse(resStr);
            RC_TEST_ASSERT(resParsed.has_value(), "Should parse 'message' field");
            RC_TEST_ASSERT(resParsed->message == "Something broke", "Message matches");
        }},

        {"Protocol: Malformed and Edge Cases", []() {
            // Empty frame
            RC_TEST_ASSERT(!parseRequest("").has_value(), "Empty frame should fail");
            RC_TEST_ASSERT(!parseResponse("\n").has_value(), "Whitespace only should fail");

            // Non-JSON syntax
            RC_TEST_ASSERT(!parseRequest("HELLO WORLD\n").has_value(), "Garbage text should fail");

            // Non-object JSON
            RC_TEST_ASSERT(!parseRequest("[1, 2, 3]\n").has_value(), "JSON array should fail");
            RC_TEST_ASSERT(!parseRequest("\"a string\"\n").has_value(), "JSON string should fail");

            // Missing op
            RC_TEST_ASSERT(!parseRequest("{\"key\":\"k1\"}").has_value(), "Missing op should fail");

            // Unknown op
            RC_TEST_ASSERT(!parseRequest("{\"op\":\"SHUTDOWN\"}").has_value(), "Unknown op should fail");

            // PUT missing value
            RC_TEST_ASSERT(!parseRequest("{\"op\":\"PUT\",\"key\":\"k1\"}").has_value(), "PUT missing value should fail");

            // PUT missing key
            RC_TEST_ASSERT(!parseRequest("{\"op\":\"PUT\",\"val\":\"v1\"}").has_value(), "PUT missing key should fail");

            // GET missing key
            RC_TEST_ASSERT(!parseRequest("{\"op\":\"GET\"}").has_value(), "GET missing key should fail");

            // DELETE missing key
            RC_TEST_ASSERT(!parseRequest("{\"op\":\"DELETE\"}").has_value(), "DELETE missing key should fail");

            // Response missing status
            RC_TEST_ASSERT(!parseResponse("{\"req_id\":1}").has_value(), "Response missing status should fail");
        }},

        {"Protocol: Bounded Frame Limit (64 KB)", []() {
            // Construct payload larger than MAX_FRAME_SIZE (64 KB)
            std::string hugePayload(MAX_FRAME_SIZE + 100, 'x');
            std::string hugeFrame = "{\"op\":\"PUT\",\"key\":\"k\",\"val\":\"" + hugePayload + "\"}\n";

            std::string err;
            auto parsed = parseRequest(hugeFrame, &err);
            RC_TEST_ASSERT(!parsed.has_value(), "Payload exceeding MAX_FRAME_SIZE should be rejected");
            RC_TEST_ASSERT(!err.empty(), "Error message should describe size bound rejection");

            auto parsedRes = parseResponse(hugeFrame, &err);
            RC_TEST_ASSERT(!parsedRes.has_value(), "Response exceeding MAX_FRAME_SIZE should be rejected");
        }}
    };
}

} // namespace ringcache::test
