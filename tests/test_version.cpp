#include "ringcache/version.hpp"
#include "test_common.hpp"

#include <stdexcept>
#include <string>
#include <vector>

namespace ringcache::test {

std::vector<TestCase> getVersionTests() {
    return {
        {
            "VersionNonNegative",
            []() {
                const auto v = ringcache::getVersion();
                if (v.major < 0 || v.minor < 0 || v.patch < 0) {
                    throw std::runtime_error("Version components must be non-negative");
                }
            }
        },
        {
            "VersionStringMatchesComponents",
            []() {
                const auto v = ringcache::getVersion();
                const std::string s = ringcache::getVersionString();
                if (s.empty()) {
                    throw std::runtime_error("Version string must not be empty");
                }
                const std::string expectedPrefix = std::to_string(v.major) + "." +
                                                  std::to_string(v.minor) + "." +
                                                  std::to_string(v.patch);
                if (s.rfind(expectedPrefix, 0) != 0) {
                    throw std::runtime_error("Version string does not start with major.minor.patch: " + s);
                }
            }
        },
        {
            "VersionIsAtLeastCheck",
            []() {
                const ringcache::Version v{1, 2, 3, ""};
                if (!v.isAtLeast(1, 2, 3)) {
                    throw std::runtime_error("Version 1.2.3 should be at least 1.2.3");
                }
                if (!v.isAtLeast(1, 1, 9)) {
                    throw std::runtime_error("Version 1.2.3 should be at least 1.1.9");
                }
                if (!v.isAtLeast(0, 9, 9)) {
                    throw std::runtime_error("Version 1.2.3 should be at least 0.9.9");
                }
                if (v.isAtLeast(1, 2, 4)) {
                    throw std::runtime_error("Version 1.2.3 must not be at least 1.2.4");
                }
                if (v.isAtLeast(1, 3, 0)) {
                    throw std::runtime_error("Version 1.2.3 must not be at least 1.3.0");
                }
                if (v.isAtLeast(2, 0, 0)) {
                    throw std::runtime_error("Version 1.2.3 must not be at least 2.0.0");
                }
            }
        },
        {
            "VersionTagFormatting",
            []() {
                const ringcache::Version withTag{0, 1, 0, "beta.1"};
                if (withTag.string() != "0.1.0-beta.1") {
                    throw std::runtime_error("Expected tag formatting '0.1.0-beta.1', got: " + withTag.string());
                }

                const ringcache::Version withoutTag{2, 0, 1, ""};
                if (withoutTag.string() != "2.0.1") {
                    throw std::runtime_error("Expected formatting '2.0.1', got: " + withoutTag.string());
                }
            }
        }
    };
}

} // namespace ringcache::test
