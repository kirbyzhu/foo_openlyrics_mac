cmake_minimum_required(VERSION 3.20)
project(foo_openlyrics_core LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)

include(FetchContent)
FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_library(openlyrics_core INTERFACE)
target_include_directories(openlyrics_core INTERFACE extensions/foo_openlyrics_mac/core)

enable_testing()
add_executable(core_tests
  tests/test_smoke.cpp
)
target_link_libraries(core_tests PRIVATE openlyrics_core GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(core_tests)
