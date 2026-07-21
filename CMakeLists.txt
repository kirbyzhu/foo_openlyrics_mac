cmake_minimum_required(VERSION 3.20)
project(foo_openlyrics_core LANGUAGES CXX C OBJC OBJCXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_library(openlyrics_core STATIC
  extensions/foo_openlyrics_mac/core/parser/LrcParser.cpp
  extensions/foo_openlyrics_mac/core/parser/LrcSerializer.cpp
  extensions/foo_openlyrics_mac/core/sync/SyncEngine.cpp
)
target_include_directories(openlyrics_core PUBLIC extensions/foo_openlyrics_mac/core)

# Plan 2 Task 1：foobar2000 macOS SDK 构建探针（fb2k_sdk 静态库），与上面的 Plan 1 核心目标相互独立。
if(EXISTS ${CMAKE_SOURCE_DIR}/SDK-2025-03-07)
  add_subdirectory(extensions/foo_openlyrics_mac/platform)
endif()

enable_testing()
add_executable(core_tests
  tests/test_smoke.cpp
  tests/test_lrc_parser.cpp
  tests/test_lrc_serializer.cpp
  tests/test_sync_engine.cpp
  tests/test_ports.cpp
)
target_link_libraries(core_tests PRIVATE openlyrics_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(core_tests)
