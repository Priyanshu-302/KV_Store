cmake_minimum_required(VERSION 3.20)

project(
    kv_store
    VERSION 1.0.0
    DESCRIPTION "High-throughput, low-latency in-memory Key-Value Store (Redis Clone) in modern C++"
    LANGUAGES CXX
)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
    add_compile_options(
        -Wall
        -Wextra
        -Wpedantic
        -Wconversion
        -Wsign-conversion
        -Wshadow
        -Wnon-virtual-dtor
        -Wold-style-cast
        -Wcast-align
        -Wunused
        -Woverloaded-virtual
        -D_GNU_SOURCE  # Expose Linux POSIX extensions (epoll, fcntl, signal flags)
    )

    if(NOT CMAKE_BUILD_TYPE STREQUAL "Debug")
        add_compile_options(
            -O3
            -march=native               # Enable AVX2/SSE4 instructions of host CPU
            -fno-omit-frame-pointer     # Essential for flame graphs and profilers (perf)
        )
    else()
        add_compile_options(-g3 -O0)
    endif()
endif()

include(cmake/Dependencies.cmake)

find_package(Threads REQUIRED)

add_library(kv_common STATIC
    src/common/logger.cpp
)
target_include_directories(kv_common PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kv_common PUBLIC
    spdlog::spdlog
    Threads::Threads
)

add_library(kv_protocol STATIC
    src/protocol/resp_parser.cpp
    src/protocol/resp_serializer.cpp
)
target_include_directories(kv_protocol PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kv_protocol PUBLIC
    kv_common
)

add_library(kv_storage STATIC
    src/storage/lru_cache.cpp
    src/storage/engine.cpp
)
target_include_directories(kv_storage PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kv_storage PUBLIC
    kv_common
)

add_library(kv_persistence STATIC
    src/persistence/aof.cpp
)
target_include_directories(kv_persistence PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kv_persistence PUBLIC
    kv_common
    kv_storage
    kv_protocol
)

add_library(kv_network STATIC
    src/network/socket.cpp
    src/network/event_loop.cpp
    src/network/connection.cpp
    src/network/server.cpp
)
target_include_directories(kv_network PUBLIC
    ${CMAKE_CURRENT_SOURCE_DIR}/include
)
target_link_libraries(kv_network PUBLIC
    kv_common
    kv_protocol
    kv_storage
    kv_persistence
)

add_library(kv_server_lib INTERFACE)
target_link_libraries(kv_server_lib INTERFACE
    kv_common
    kv_protocol
    kv_storage
    kv_persistence
    kv_network
)

add_executable(kv_server
    src/main.cpp
)
target_link_libraries(kv_server PRIVATE
    kv_server_lib
    CLI11::CLI11
)

enable_testing()
add_subdirectory(tests)
