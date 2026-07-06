cmake_minimum_required(VERSION 3.20)
project(matching_engine LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

add_compile_options(-O2 -Wall -Wextra -Wpedantic -Werror)

# ---------------------------------------------------------------------------
# Core engine library
# ---------------------------------------------------------------------------
add_library(engine STATIC
    src/order_book.cpp
    src/order_book_v1.cpp
    src/order_book_v2.cpp
    src/matching_engine.cpp
    src/itch/itch_parser.cpp
)
target_include_directories(engine PUBLIC src/)

# ---------------------------------------------------------------------------
# Main binary
# ---------------------------------------------------------------------------
add_executable(matching_engine src/main.cpp)
target_link_libraries(matching_engine PRIVATE engine)

# ---------------------------------------------------------------------------
# Tests
# ---------------------------------------------------------------------------
find_package(GTest REQUIRED)
add_executable(tests
    tests/test_order_book.cpp
    tests/test_matching.cpp
    tests/test_spsc.cpp
    tests/test_itch_parser.cpp
)
target_link_libraries(tests PRIVATE engine GTest::GTest GTest::Main pthread)
include(GoogleTest)
gtest_discover_tests(tests)

# ---------------------------------------------------------------------------
# Benchmarks
# ---------------------------------------------------------------------------
add_executable(bench_order_book benchmarks/bench_order_book.cpp)
target_link_libraries(bench_order_book PRIVATE engine)
target_compile_options(bench_order_book PRIVATE -O3)

add_executable(bench_matching benchmarks/bench_matching.cpp)
target_link_libraries(bench_matching PRIVATE engine)
target_compile_options(bench_matching PRIVATE -O3)

add_executable(bench_spsc benchmarks/bench_spsc.cpp)
target_link_libraries(bench_spsc PRIVATE engine)
target_compile_options(bench_spsc PRIVATE -O3)
