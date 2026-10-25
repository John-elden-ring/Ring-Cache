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

if(POLICY CMP0135)
    cmake_policy(SET CMP0135 NEW)
endif()
if(POLICY CMP0169)
    cmake_policy(SET CMP0169 OLD)
endif()
set(CMAKE_TLS_VERIFY OFF)

include(FetchContent)

# Standalone Asio (header-only, no Boost required)
FetchContent_Declare(
    asio
    URL https://github.com/chriskohlhoff/asio/archive/refs/tags/asio-1-30-2.tar.gz
)
FetchContent_GetProperties(asio)
if(NOT asio_POPULATED)
    FetchContent_Populate(asio)
    add_library(asio INTERFACE)
    target_include_directories(asio INTERFACE ${asio_SOURCE_DIR}/asio/include)
    target_compile_definitions(asio INTERFACE ASIO_STANDALONE)
    if(WIN32)
        target_link_libraries(asio INTERFACE ws2_32 mswsock)
    endif()
endif()

# nlohmann/json
set(JSON_BuildTests OFF CACHE INTERNAL "")
set(JSON_Install OFF CACHE INTERNAL "")
FetchContent_Declare(
    nlohmann_json
    URL https://github.com/nlohmann/json/archive/refs/tags/v3.11.3.tar.gz
)
FetchContent_MakeAvailable(nlohmann_json)


# Library target
add_library(ringcache
    src/cache_node.cpp
    src/cluster_router.cpp
    src/hash.cpp
    src/hash_ring.cpp
    src/lru_cache.cpp
    src/network_client.cpp
    src/network_cluster_router.cpp
    src/protocol.cpp
    src/tcp_server.cpp
    src/tcp_session.cpp
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

target_link_libraries(ringcache
    PUBLIC
        asio
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(ringcache PRIVATE /W4 /permissive-)
else()
    target_compile_options(ringcache PRIVATE -Wall -Wextra -Wpedantic)
endif()

# Executables
add_executable(ringcache_node apps/node_main.cpp)
target_link_libraries(ringcache_node PRIVATE ringcache)
target_compile_features(ringcache_node PRIVATE cxx_std_17)

add_executable(ringcache_cli apps/cli_main.cpp)
target_link_libraries(ringcache_cli PRIVATE ringcache)
target_compile_features(ringcache_cli PRIVATE cxx_std_17)

# Automated testing
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()
