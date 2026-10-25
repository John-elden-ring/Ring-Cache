cmake_minimum_required(VERSION 3.16)

project(RingCache
    VERSION 0.1.0
    DESCRIPTION "Distributed in-memory key-value cache using consistent hashing"
    LANGUAGES CXX
)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Library target
add_library(ringcache
    src/hash.cpp
    src/hash_ring.cpp
    src/lru_cache.cpp
    src/version.cpp
)

target_include_directories(ringcache
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:include>
)

target_compile_features(ringcache
    PUBLIC
        cxx_std_17
)

if(MSVC)
    target_compile_options(ringcache PRIVATE /W4 /permissive-)
else()
    target_compile_options(ringcache PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Automated testing
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
