#include "test_common.hpp"

#include <iostream>
#include <vector>

namespace ringcache::test {
std::vector<TestCase> getVersionTests();
std::vector<TestCase> getHashRingTests();
std::vector<TestCase> getLruCacheTests();
std::vector<TestCase> getClusterRouterTests();
std::vector<TestCase> getProtocolTests();
std::vector<TestCase> getNetworkIntegrationTests();
std::vector<TestCase> getFailureResilienceTests();
std::vector<TestCase> getClientTests();
} // namespace ringcache::test

#ifndef _WIN32
#include <csignal>
#endif

int main() {
#ifndef _WIN32
    std::signal(SIGPIPE, SIG_IGN);
#endif
    std::vector<ringcache::test::TestCase> allTests;

    const auto versionTests = ringcache::test::getVersionTests();
    allTests.insert(allTests.end(), versionTests.begin(), versionTests.end());

    const auto hashRingTests = ringcache::test::getHashRingTests();
    allTests.insert(allTests.end(), hashRingTests.begin(), hashRingTests.end());

    const auto lruCacheTests = ringcache::test::getLruCacheTests();
    allTests.insert(allTests.end(), lruCacheTests.begin(), lruCacheTests.end());

    const auto clusterRouterTests = ringcache::test::getClusterRouterTests();
    allTests.insert(allTests.end(), clusterRouterTests.begin(), clusterRouterTests.end());

    const auto protocolTests = ringcache::test::getProtocolTests();
    allTests.insert(allTests.end(), protocolTests.begin(), protocolTests.end());

    const auto networkIntegrationTests = ringcache::test::getNetworkIntegrationTests();
    allTests.insert(allTests.end(), networkIntegrationTests.begin(), networkIntegrationTests.end());

    const auto failureResilienceTests = ringcache::test::getFailureResilienceTests();
    allTests.insert(allTests.end(), failureResilienceTests.begin(), failureResilienceTests.end());

    const auto clientTests = ringcache::test::getClientTests();
    allTests.insert(allTests.end(), clientTests.begin(), clientTests.end());

    int passed = 0;
    int failed = 0;

    std::cout << "Running " << allTests.size() << " test(s) for ringcache...\n";

    for (const auto& test : allTests) {
        std::cout << "  [ RUN      ] " << test.name << std::endl;
        try {
            test.func();
            std::cout << "  [       OK ] " << test.name << std::endl;
            passed++;
        } catch (const std::exception& ex) {
            std::cerr << "  [  FAILED  ] " << test.name << " (" << ex.what() << ")" << std::endl;
            failed++;
        } catch (...) {
            std::cerr << "  [  FAILED  ] " << test.name << " (unknown exception)" << std::endl;
            failed++;
        }
    }

    std::cout << "\nTest Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
