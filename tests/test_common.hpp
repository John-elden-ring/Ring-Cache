#pragma once

#include <functional>
#include <sstream>
#include <stdexcept>
#include <string>

namespace ringcache::test {

struct TestCase {
    std::string name;
    std::function<void()> func;
};

#define RC_TEST_ASSERT(condition, message) \
    do { \
        if (!(condition)) { \
            std::ostringstream rc_oss; \
            rc_oss << "Assertion failed: (" #condition ") - " << (message); \
            throw std::runtime_error(rc_oss.str()); \
        } \
    } while (false)

} // namespace ringcache::test
