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


include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

# ==============================================================================
# 1. Reusable Client Library (RingCache::client)
# ==============================================================================
add_library(ringcache_client
    src/client.cpp
    src/cluster_router.cpp
    src/hash.cpp
    src/hash_ring.cpp
    src/network_client.cpp
    src/network_cluster_router.cpp
    src/protocol.cpp
    src/version.cpp
)

add_library(RingCache::client ALIAS ringcache_client)
add_library(RingCache::ringcache ALIAS ringcache_client)

target_include_directories(ringcache_client
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
        $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>
)

target_compile_features(ringcache_client
    PUBLIC
        cxx_std_17
)

target_link_libraries(ringcache_client
    PUBLIC
        $<BUILD_INTERFACE:asio>
        $<BUILD_INTERFACE:nlohmann_json::nlohmann_json>
        $<$<PLATFORM_ID:Windows>:ws2_32>
        $<$<PLATFORM_ID:Windows>:mswsock>
)

if(MSVC)
    target_compile_options(ringcache_client PRIVATE /W4 /permissive-)
else()
    target_compile_options(ringcache_client PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ==============================================================================
# 2. Server Engine Library (Internal / Daemon)
# ==============================================================================
add_library(ringcache_server
    src/cache_node.cpp
    src/lru_cache.cpp
    src/tcp_server.cpp
    src/tcp_session.cpp
)

add_library(RingCache::server ALIAS ringcache_server)
# Alias for internal backwards compatibility
add_library(ringcache ALIAS ringcache_server)

target_include_directories(ringcache_server
    PUBLIC
        $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
)

target_compile_features(ringcache_server
    PUBLIC
        cxx_std_17
)

target_link_libraries(ringcache_server
    PUBLIC
        ringcache_client
        asio
        nlohmann_json::nlohmann_json
)

if(MSVC)
    target_compile_options(ringcache_server PRIVATE /W4 /permissive-)
else()
    target_compile_options(ringcache_server PRIVATE -Wall -Wextra -Wpedantic)
endif()

# ==============================================================================
# 3. Executables
# ==============================================================================
add_executable(ringcache_node apps/node_main.cpp)
target_link_libraries(ringcache_node PRIVATE ringcache_server)
target_compile_features(ringcache_node PRIVATE cxx_std_17)

add_executable(ringcache_cli apps/cli_main.cpp)
target_link_libraries(ringcache_cli PRIVATE ringcache_client)
target_compile_features(ringcache_cli PRIVATE cxx_std_17)

add_executable(ringcache_bench apps/bench_main.cpp)
target_link_libraries(ringcache_bench PRIVATE ringcache_client)
target_compile_features(ringcache_bench PRIVATE cxx_std_17)

# ==============================================================================
# 4. Installation & Export Configuration
# ==============================================================================
# Install reusable client library and export targets
install(TARGETS ringcache_client
    EXPORT RingCacheTargets
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Install public C++ API headers
install(FILES
    include/ringcache/client.hpp
    include/ringcache/version.hpp
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR}/ringcache
)

# Install executable tools
install(TARGETS ringcache_node ringcache_cli ringcache_bench
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
)

# Export namespaced CMake targets
install(EXPORT RingCacheTargets
    FILE RingCacheTargets.cmake
    NAMESPACE RingCache::
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RingCache
)

# Package version and config generation
write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/RingCacheConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
)

configure_package_config_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/RingCacheConfig.cmake.in"
    "${CMAKE_CURRENT_BINARY_DIR}/RingCacheConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/RingCache"
)

install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/RingCacheConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/RingCacheConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/RingCache
)

# ==============================================================================
# 5. Automated Testing
# ==============================================================================
include(CTest)
if(BUILD_TESTING)
    add_subdirectory(tests)
endif()

