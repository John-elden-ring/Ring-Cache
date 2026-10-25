#pragma once

#include <string>
#include <string_view>

namespace ringcache {

struct Version {
    int major{0};
    int minor{1};
    int patch{0};
    std::string_view tag{""};

    [[nodiscard]] std::string string() const;
    [[nodiscard]] bool isAtLeast(int requiredMajor, int requiredMinor, int requiredPatch) const noexcept;
};

[[nodiscard]] Version getVersion() noexcept;
[[nodiscard]] std::string getVersionString();

} // namespace ringcache
