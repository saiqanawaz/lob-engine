cmake_minimum_required(VERSION 3.20)
project(lob_engine VERSION 0.1.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()

add_library(lob
  src/order_book.cpp
  src/matching_engine.cpp
)
target_include_directories(lob PUBLIC ${CMAKE_CURRENT_SOURCE_DIR}/include)
set_target_properties(lob PROPERTIES POSITION_INDEPENDENT_CODE ON)
target_compile_options(lob PRIVATE
  $<$<CXX_COMPILER_ID:GNU,Clang>:-Wall -Wextra -Wpedantic>
  $<$<CXX_COMPILER_ID:MSVC>:/W4>
)

option(LOB_BUILD_TESTS "Build unit tests" ON)
if(LOB_BUILD_TESTS)
  enable_testing()
  add_subdirectory(tests)
endif()

option(LOB_BUILD_BENCHMARKS "Build benchmarks" ON)
if(LOB_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

option(LOB_BUILD_PYTHON "Build pybind11 Python module" ON)
if(LOB_BUILD_PYTHON)
  add_subdirectory(python)
endif()
