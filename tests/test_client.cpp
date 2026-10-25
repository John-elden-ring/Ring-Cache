#include "ringcache/client.hpp"
#include "test_common.hpp"
#include "test_process_helpers.hpp"

#include <memory>
#include <string>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getClientTests() {
    return {
        {"Client: Construction and Configuration Validation", []() {
            // Empty peers should throw
            bool threwEmpty = false;
            try {
                ClientConfig cfg;
                cfg.peers = "";
                Client c(cfg);
            } catch (const std::invalid_argument&) {
                threwEmpty = true;
            }
            RC_TEST_ASSERT(threwEmpty, "Empty peers must throw invalid_argument");

            // Malformed peers should throw
            bool threwMalformed = false;
            try {
                Client c("invalid_format_without_equal_or_colon");
            } catch (const std::invalid_argument&) {
                threwMalformed = true;
            }
            RC_TEST_ASSERT(threwMalformed, "Malformed peers must throw invalid_argument");

            // Valid peers should succeed
            ClientConfig validCfg;
            validCfg.peers = "n1=127.0.0.1:8001,n2=127.0.0.1:8002";
            validCfg.replicationFactor = 2;
            Client client(validCfg);

            RC_TEST_ASSERT(client.nodeCount() == 2, "Configured node count must match parsed peers");
            RC_TEST_ASSERT(client.getReplicationFactor() == 2, "Configured replication factor must match");
        }},

        {"Client: Move Semantics and Lifetimes", []() {
            Client c1("n1=127.0.0.1:8001");
            RC_TEST_ASSERT(c1.nodeCount() == 1, "Initial node count");

            // Move construct
            Client c2(std::move(c1));
            RC_TEST_ASSERT(c2.nodeCount() == 1, "Moved node count");

            // Move assign
            Client c3("n2=127.0.0.1:8002");
            c3 = std::move(c2);
            RC_TEST_ASSERT(c3.nodeCount() == 1, "Move assigned node count");
        }},

        {"Client: Live Cluster Operations and Explicit Error Handling", []() {
            std::string binary = findNodeBinary();
            RC_TEST_ASSERT(!binary.empty(), "ringcache_node binary must exist");

            const uint16_t port = 18751;
            std::string cmd = "\"" + binary + "\" --id node-pub --port " + std::to_string(port) + " --capacity 200";

            auto proc = std::make_unique<ChildProcess>();
            RC_TEST_ASSERT(proc->start(cmd), "Start test node");
            RC_TEST_ASSERT(waitForNodeReady(port), "Wait for node ready");

            Client client("node-pub=127.0.0.1:" + std::to_string(port), 1);

            // Ping
            ClientError err;
            RC_TEST_ASSERT(client.ping("node-pub", &err), "Ping specific node should succeed");
            RC_TEST_ASSERT(err.ok(), "Error should be ok on successful ping");

            RC_TEST_ASSERT(client.pingAll(&err), "pingAll should succeed");
            RC_TEST_ASSERT(err.ok(), "Error should be ok on pingAll");

            // Ping non-existent node ID
            bool pingBogus = client.ping("non_existent_node", &err);
            RC_TEST_ASSERT(!pingBogus, "Ping non-existent node must fail");
            RC_TEST_ASSERT(err.code == ErrorCode::ConnectionFailed, "Error code should be ConnectionFailed");

            // PUT
            RC_TEST_ASSERT(client.put("sample_key", "sample_value", &err), "PUT must succeed");
            RC_TEST_ASSERT(err.ok(), "Error should be ok on successful put");

            // PUT empty key validation
            bool putEmpty = client.put("", "val", &err);
            RC_TEST_ASSERT(!putEmpty, "PUT with empty key must fail");
            RC_TEST_ASSERT(err.code == ErrorCode::InvalidConfiguration, "Empty key gives InvalidConfiguration");

            // GET
            auto val = client.get("sample_key", &err);
            RC_TEST_ASSERT(val.has_value() && *val == "sample_value", "GET must retrieve stored value");
            RC_TEST_ASSERT(err.ok(), "Error should be ok on successful get");

            // GET non-existent key
            auto missingVal = client.get("key_not_there", &err);
            RC_TEST_ASSERT(!missingVal.has_value(), "Missing key must return nullopt");
            RC_TEST_ASSERT(err.code == ErrorCode::KeyNotFound, "Missing key sets KeyNotFound code");

            // PUT with metrics
            auto metrics = client.putWithMetrics("metric_key", "metric_val", &err);
            RC_TEST_ASSERT(metrics.success, "putWithMetrics must succeed");
            RC_TEST_ASSERT(metrics.writtenCount == 1, "writtenCount must be 1 for R=1");
            RC_TEST_ASSERT(metrics.primaryNode == "node-pub", "Primary node should be node-pub");

            // DEL
            RC_TEST_ASSERT(client.del("sample_key", &err), "DEL should succeed");
            RC_TEST_ASSERT(err.ok(), "DEL ok");
            RC_TEST_ASSERT(!client.get("sample_key").has_value(), "Key must be deleted");

            // Stop node and test error code on unreachable
            proc->stop();

            auto deadVal = client.get("metric_key", &err);
            RC_TEST_ASSERT(!deadVal.has_value(), "GET on dead node returns nullopt");
            RC_TEST_ASSERT(!err.ok(), "Error must be signaled when node is dead");
        }}
    };
}

} // namespace ringcache::test
