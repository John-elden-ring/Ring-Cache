#pragma once

#include <functional>
#include <string>

namespace ringcache::test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

} // namespace ringcache::test
