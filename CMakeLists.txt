cmake_minimum_required(VERSION 3.14)
project(stopwatch LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

add_library(stopwatch INTERFACE)
add_library(stopwatch::stopwatch ALIAS stopwatch)
target_include_directories(
  stopwatch INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>)
target_compile_features(stopwatch INTERFACE cxx_std_17)

option(STOPWATCH_BUILD_TESTS "Build tests" OFF)

if(STOPWATCH_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)

  add_executable(stopwatch_tests tests/stopwatch_test.cc)
  target_link_libraries(stopwatch_tests PRIVATE stopwatch GTest::gtest_main)
  set_target_properties(stopwatch_tests PROPERTIES CXX_EXTENSIONS OFF)
  if(MSVC)
    target_compile_options(stopwatch_tests PRIVATE /W4 /Zc:__cplusplus)
  else()
    target_compile_options(stopwatch_tests PRIVATE -Wall -Wextra -Wpedantic)
  endif()

  include(GoogleTest)
  gtest_discover_tests(stopwatch_tests)
endif()
