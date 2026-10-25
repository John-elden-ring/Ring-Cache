#include <ringcache/client.hpp>
#include <ringcache/version.hpp>

#include <iostream>

int main() {
    std::cout << "Testing RingCache Conan Package v" << ringcache::getVersionString() << "\n";
    try {
        ringcache::Client client("test_node=127.0.0.1:8001");
        std::cout << "RingCache client successfully initialized with " << client.nodeCount() << " node(s).\n";
        return 0;
    } catch (const std::exception& ex) {
        std::cerr << "Initialization error: " << ex.what() << "\n";
        return 1;
    }
}
