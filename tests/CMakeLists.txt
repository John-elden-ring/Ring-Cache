add_executable(ringcache_tests
    test_main.cpp
    test_version.cpp
    test_hash_ring.cpp
)

target_link_libraries(ringcache_tests
    PRIVATE
        ringcache
)

target_compile_features(ringcache_tests
    PRIVATE
        cxx_std_17
)

if(MSVC)
    target_compile_options(ringcache_tests PRIVATE /W4 /permissive-)
else()
    target_compile_options(ringcache_tests PRIVATE -Wall -Wextra -Wpedantic)
endif()

add_test(NAME ringcache_tests COMMAND ringcache_tests)
