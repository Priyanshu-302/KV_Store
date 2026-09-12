# ==============================================================================
# Catch2 v3 Unit & Integration Test Suite Configuration
# ==============================================================================

add_executable(kv_tests
    test_engine.cpp
    test_resp.cpp
    test_aof.cpp
)

target_include_directories(kv_tests PRIVATE
    ${CMAKE_SOURCE_DIR}/include
)

target_link_libraries(kv_tests PRIVATE
    kv_server_lib
    Catch2::Catch2WithMain
)

# Integrate with CTest test runner
include(CTest)
include(Catch)
catch_discover_tests(kv_tests)
