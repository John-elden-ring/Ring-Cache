#include "ringcache/version.hpp"

#include <sstream>

namespace ringcache {

std::string Version::string() const {
    std::ostringstream oss;
    oss << major << '.' << minor << '.' << patch;
    if (!tag.empty()) {
        oss << '-' << tag;
    }
    return oss.str();
}

bool Version::isAtLeast(int requiredMajor, int requiredMinor, int requiredPatch) const noexcept {
    if (major != requiredMajor) {
        return major > requiredMajor;
    }
    if (minor != requiredMinor) {
        return minor > requiredMinor;
    }
    return patch >= requiredPatch;
}

Version getVersion() noexcept {
    return Version{0, 1, 0, "dev"};
}

std::string getVersionString() {
    return getVersion().string();
}

} // namespace ringcache
