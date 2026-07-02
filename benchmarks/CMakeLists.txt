include(FetchContent)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_GTEST_TESTS OFF CACHE BOOL "" FORCE)
set(BENCHMARK_ENABLE_INSTALL OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
  googlebenchmark
  GIT_REPOSITORY https://github.com/google/benchmark.git
  GIT_TAG        v1.8.4
  GIT_SHALLOW    TRUE
)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(lob_bench bench_engine.cpp)
target_link_libraries(lob_bench PRIVATE lob benchmark::benchmark)

add_executable(lob_latency latency_histogram.cpp)
target_link_libraries(lob_latency PRIVATE lob)
