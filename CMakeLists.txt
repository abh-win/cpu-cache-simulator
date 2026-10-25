cmake_minimum_required(VERSION 3.16)
project(cachesim CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE Release)
endif()
add_compile_options(-Wall -Wextra -Wpedantic)

add_library(cachesim
  src/cache.cpp
  src/classifier.cpp
  src/config.cpp
  src/hierarchy.cpp
  src/replacement.cpp
  src/trace.cpp
)
target_include_directories(cachesim PUBLIC include)

add_executable(cachesim_cli tools/cachesim.cpp)
set_target_properties(cachesim_cli PROPERTIES OUTPUT_NAME cachesim)
target_link_libraries(cachesim_cli PRIVATE cachesim)

add_executable(tracegen tools/tracegen.cpp)
target_link_libraries(tracegen PRIVATE cachesim)

add_executable(sweep tools/sweep.cpp)
target_link_libraries(sweep PRIVATE cachesim)

option(CACHESIM_BUILD_TESTS "Build unit tests" ON)
if(CACHESIM_BUILD_TESTS)
  find_package(GTest REQUIRED)
  enable_testing()
  add_executable(cachesim_tests
    tests/cache_test.cpp
    tests/classifier_test.cpp
    tests/hierarchy_test.cpp
    tests/replacement_test.cpp
    tests/trace_test.cpp
  )
  target_link_libraries(cachesim_tests PRIVATE cachesim GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(cachesim_tests)
endif()
