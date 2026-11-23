cmake_minimum_required(VERSION 3.14)
project(stopwatch VERSION 1.2.0 LANGUAGES CXX)

if(POLICY CMP0135)
  cmake_policy(SET CMP0135 NEW)
endif()

include(GNUInstallDirs)

string(COMPARE EQUAL "${CMAKE_SOURCE_DIR}" "${CMAKE_CURRENT_SOURCE_DIR}"
       STOPWATCH_IS_TOP_LEVEL)

add_library(stopwatch INTERFACE)
add_library(stopwatch::stopwatch ALIAS stopwatch)
target_include_directories(
  stopwatch
  INTERFACE $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
            $<INSTALL_INTERFACE:${CMAKE_INSTALL_INCLUDEDIR}>)
target_compile_features(stopwatch INTERFACE cxx_std_17)

option(STOPWATCH_BUILD_TESTS "Build tests" OFF)
option(STOPWATCH_INSTALL "Generate install targets" ${STOPWATCH_IS_TOP_LEVEL})

if(STOPWATCH_INSTALL)
  include(CMakePackageConfigHelpers)

  install(
    TARGETS stopwatch
    EXPORT stopwatchTargets)
  install(
    FILES include/stopwatch.h
    DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  configure_package_config_file(
    cmake/stopwatchConfig.cmake.in
    "${CMAKE_CURRENT_BINARY_DIR}/stopwatchConfig.cmake"
    INSTALL_DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stopwatch")

  write_basic_package_version_file(
    "${CMAKE_CURRENT_BINARY_DIR}/stopwatchConfigVersion.cmake"
    VERSION ${PROJECT_VERSION}
    COMPATIBILITY SameMajorVersion
    ARCH_INDEPENDENT)

  install(
    EXPORT stopwatchTargets
    FILE stopwatchTargets.cmake
    NAMESPACE stopwatch::
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stopwatch")

  install(
    FILES "${CMAKE_CURRENT_BINARY_DIR}/stopwatchConfig.cmake"
          "${CMAKE_CURRENT_BINARY_DIR}/stopwatchConfigVersion.cmake"
    DESTINATION "${CMAKE_INSTALL_LIBDIR}/cmake/stopwatch")
endif()

if(STOPWATCH_BUILD_TESTS)
  enable_testing()

  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    URL_HASH SHA256=8ad598c73ad796e0d8280b082cebd82a630d73e73cd3c70057938a6501bba5d7
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
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
