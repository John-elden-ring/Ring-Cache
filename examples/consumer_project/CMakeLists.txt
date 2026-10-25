cmake_minimum_required(VERSION 3.16)
project(RingCacheConsumerExample LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

# Locate the installed RingCache package
find_package(RingCache CONFIG REQUIRED)

add_executable(consumer_app main.cpp)

# Link the namespaced client target exported by RingCache
target_link_libraries(consumer_app PRIVATE RingCache::client)

if(MSVC)
    target_compile_options(consumer_app PRIVATE /W4 /permissive-)
else()
    target_compile_options(consumer_app PRIVATE -Wall -Wextra -Wpedantic)
endif()
