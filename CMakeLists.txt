cmake_minimum_required(VERSION 3.14)
project(TaskFlow VERSION 2.0.0 LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

find_package(Threads REQUIRED)

find_package(nlohmann_json QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, fetching from GitHub...")
  include(FetchContent)
  FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.2
  )
  FetchContent_MakeAvailable(json)
endif()

add_library(taskflow_core
  src/workflow/blueprint.cpp
  src/workflow/serializer.cpp
  src/engine/execution.cpp
  src/engine/registry.cpp
  src/engine/scheduler.cpp
  src/engine/executor.cpp
  src/engine/orchestrator.cpp
  src/storage/memory_storage.cpp
)

add_library(TaskFlow::taskflow_core ALIAS taskflow_core)
add_library(TaskFlow::TaskFlow ALIAS taskflow_core)

target_include_directories(taskflow_core PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskflow_core PUBLIC
  Threads::Threads
  nlohmann_json::nlohmann_json
)

option(TASKFLOW_BUILD_TESTS "Build unit tests" ON)
option(TASKFLOW_BUILD_BENCHMARKS "Build benchmarks" ON)
option(TASKFLOW_BUILD_EXAMPLES "Build examples" ON)

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND TASKFLOW_BUILD_TESTS)
  include(FetchContent)
  FetchContent_Declare(
    googletest
    URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  )
  set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googletest)
  enable_testing()
  add_executable(taskflow_tests
    tests/orchestrator_test.cpp
  )
  target_link_libraries(taskflow_tests PRIVATE taskflow_core GTest::gtest_main)
  include(GoogleTest)
  gtest_discover_tests(taskflow_tests)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND TASKFLOW_BUILD_BENCHMARKS)
  include(FetchContent)
  FetchContent_Declare(
    googlebenchmark
    URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
  )
  set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
  FetchContent_MakeAvailable(googlebenchmark)
  add_executable(taskflow_benchmark
    benchmarks/orchestrator_benchmark.cpp
  )
  target_link_libraries(taskflow_benchmark PRIVATE taskflow_core benchmark::benchmark_main)
endif()

if(CMAKE_PROJECT_NAME STREQUAL PROJECT_NAME AND TASKFLOW_BUILD_EXAMPLES)
  add_executable(minimal_orchestrator examples/minimal_orchestrator.cpp)
  target_link_libraries(minimal_orchestrator PRIVATE taskflow_core)
endif()

include(GNUInstallDirs)
include(CMakePackageConfigHelpers)

install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

install(TARGETS taskflow_core
  EXPORT TaskFlowTargets
)

install(EXPORT TaskFlowTargets
  FILE TaskFlowTargets.cmake
  NAMESPACE TaskFlow::
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskFlow
)

configure_package_config_file(
  "${CMAKE_CURRENT_SOURCE_DIR}/cmake/TaskFlowConfig.cmake.in"
  "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfig.cmake"
  INSTALL_DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskFlow
)

write_basic_package_version_file(
  "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfigVersion.cmake"
  VERSION ${PROJECT_VERSION}
  COMPATIBILITY SameMajorVersion
)

install(FILES
  "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfig.cmake"
  "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfigVersion.cmake"
  DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskFlow
)
