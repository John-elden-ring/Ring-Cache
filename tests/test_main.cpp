#include "test_common.hpp"

#include <iostream>
#include <vector>

namespace ringcache::test {
std::vector<TestCase> getVersionTests();
std::vector<TestCase> getHashRingTests();
} // namespace ringcache::test

int main() {
    std::vector<ringcache::test::TestCase> allTests;

    const auto versionTests = ringcache::test::getVersionTests();
    allTests.insert(allTests.end(), versionTests.begin(), versionTests.end());

    const auto hashRingTests = ringcache::test::getHashRingTests();
    allTests.insert(allTests.end(), hashRingTests.begin(), hashRingTests.end());

    int passed = 0;
    int failed = 0;

    std::cout << "Running " << allTests.size() << " test(s) for ringcache...\n";

    for (const auto& test : allTests) {
        std::cout << "  [ RUN      ] " << test.name << "\n";
        try {
            test.func();
            std::cout << "  [       OK ] " << test.name << "\n";
            passed++;
        } catch (const std::exception& ex) {
            std::cerr << "  [  FAILED  ] " << test.name << " (" << ex.what() << ")\n";
            failed++;
        } catch (...) {
            std::cerr << "  [  FAILED  ] " << test.name << " (unknown exception)\n";
            failed++;
        }
    }

    std::cout << "\nTest Results: " << passed << " passed, " << failed << " failed.\n";
    return (failed == 0) ? 0 : 1;
}
