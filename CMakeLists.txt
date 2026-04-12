cmake_minimum_required(VERSION 3.14...3.20)
project(TaskFlow VERSION 2.0.0 LANGUAGES CXX
  DESCRIPTION "Lightweight C++ task orchestration engine"
  HOMEPAGE_URL "https://github.com/merlotqi/taskflow"
)

enable_testing()

# Prevent in-source builds
if(CMAKE_SOURCE_DIR STREQUAL CMAKE_BINARY_DIR)
  message(FATAL_ERROR "In-source builds are not allowed. Please use a separate build directory.")
endif()

# Build type configuration
if(NOT CMAKE_BUILD_TYPE)
  set(CMAKE_BUILD_TYPE "Release" CACHE STRING "Build type" FORCE)
endif()
set_property(CACHE CMAKE_BUILD_TYPE PROPERTY STRINGS
  "Debug" "Release" "MinSizeRel" "RelWithDebInfo"
)

# C++ standard (C++20 optional for concepts mirror)
option(TASKFLOW_CXX20 "Build as C++20 (enables task_callable concept on task_wrapper)" OFF)
if(TASKFLOW_CXX20)
  set(CMAKE_CXX_STANDARD 20)
else()
  set(CMAKE_CXX_STANDARD 17)
endif()
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

# Compiler options
if(CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang")
  add_compile_options(-Wall -Wextra -Wpedantic)
  if(CMAKE_BUILD_TYPE STREQUAL "Debug")
    add_compile_options(-g -O0)
  endif()
elseif(MSVC)
  add_compile_options(/W4)
  add_definitions(-D_CRT_SECURE_NO_WARNINGS)
endif()

# Find dependencies
find_package(Threads REQUIRED)
find_package(nlohmann_json CONFIG QUIET)
if(NOT nlohmann_json_FOUND)
  message(STATUS "nlohmann_json not found, fetching via FetchContent...")
  include(FetchContent)
  FetchContent_Declare(
    json
    GIT_REPOSITORY https://github.com/nlohmann/json.git
    GIT_TAG v3.11.3
    DOWNLOAD_EXTRACT_TIMESTAMP TRUE
  )
  FetchContent_MakeAvailable(json)
endif()

option(TASKFLOW_WITH_SQLITE "Build optional SQLite state_storage backend" ON)
if(TASKFLOW_WITH_SQLITE)
  find_package(SQLite3 QUIET)
  if(NOT SQLite3_FOUND)
    message(WARNING "SQLite3 not found; disabling TASKFLOW_WITH_SQLITE (install libsqlite3-dev or set TASKFLOW_WITH_SQLITE=OFF)")
    set(TASKFLOW_WITH_SQLITE OFF CACHE BOOL "Build optional SQLite state_storage backend" FORCE)
  else()
    message(STATUS "SQLite3 found: ${SQLite3_VERSION}")
    message(STATUS "SQLite3 include dir: ${SQLite3_INCLUDE_DIRS}")
  endif()
endif()

# Options
option(TASKFLOW_BUILD_TESTS "Build unit tests" ON)
option(TASKFLOW_BUILD_BENCHMARKS "Build benchmarks" ON)
option(TASKFLOW_BUILD_EXAMPLES "Build examples" ON)
option(TASKFLOW_BUILD_NODE_BINDINGS "Build Node.js bindings" ON)
option(TASKFLOW_BUILD_PYTHON_BINDINGS "Build Python bindings" ON)
option(TASKFLOW_INSTALL "Generate install target" ON)

# Core library
add_subdirectory(src)
add_subdirectory(capi)

if(TASKFLOW_BUILD_NODE_BINDINGS)
  add_subdirectory(node)
endif()

if(TASKFLOW_BUILD_PYTHON_BINDINGS)
  add_subdirectory(Python)
endif()

# Set version properties
set_target_properties(taskflow PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)
set_target_properties(taskflow_c PROPERTIES
  VERSION ${PROJECT_VERSION}
  SOVERSION ${PROJECT_VERSION_MAJOR}
)

# Optional modules
if(TASKFLOW_BUILD_TESTS)
  add_subdirectory(tests)
endif()

if(TASKFLOW_BUILD_BENCHMARKS)
  add_subdirectory(benchmarks)
endif()

if(TASKFLOW_BUILD_EXAMPLES)
  add_subdirectory(examples)
endif()

# Installation
if(TASKFLOW_INSTALL)
  include(GNUInstallDirs)
  include(CMakePackageConfigHelpers)

  install(DIRECTORY include/ DESTINATION ${CMAKE_INSTALL_INCLUDEDIR})

  install(TARGETS taskflow taskflow_c
    EXPORT TaskFlowTargets
    LIBRARY DESTINATION ${CMAKE_INSTALL_LIBDIR}
    ARCHIVE DESTINATION ${CMAKE_INSTALL_LIBDIR}
    RUNTIME DESTINATION ${CMAKE_INSTALL_BINDIR}
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

  # pkg-config support
  configure_file(
    "${CMAKE_CURRENT_SOURCE_DIR}/cmake/taskflow.pc.in"
    "${CMAKE_CURRENT_BINARY_DIR}/taskflow.pc"
    @ONLY
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfig.cmake"
    "${CMAKE_CURRENT_BINARY_DIR}/TaskFlowConfigVersion.cmake"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/cmake/TaskFlow
  )

  install(FILES
    "${CMAKE_CURRENT_BINARY_DIR}/taskflow.pc"
    DESTINATION ${CMAKE_INSTALL_LIBDIR}/pkgconfig
  )

  # Uninstall target
  if(NOT TARGET uninstall)
    configure_file(
      "${CMAKE_CURRENT_SOURCE_DIR}/cmake/cmake_uninstall.cmake.in"
      "${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake"
      IMMEDIATE @ONLY
    )
    add_custom_target(uninstall
      COMMAND ${CMAKE_COMMAND} -P ${CMAKE_CURRENT_BINARY_DIR}/cmake_uninstall.cmake
      COMMENT "Uninstalling TaskFlow"
    )
  endif()
endif()

# CPack support
set(CPACK_PACKAGE_NAME "taskflow")
set(CPACK_PACKAGE_VERSION ${PROJECT_VERSION})
set(CPACK_PACKAGE_CONTACT "merlotqi")
set(CPACK_PACKAGE_DESCRIPTION_SUMMARY ${PROJECT_DESCRIPTION})
set(CPACK_GENERATOR "TGZ;ZIP")
include(CPack)
