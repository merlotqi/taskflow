include(FetchContent)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googletest)

add_executable(taskflow_tests
  orchestrator_test.cpp
)

target_link_libraries(taskflow_tests PRIVATE taskflow GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(taskflow_tests DISCOVERY_MODE PRE_TEST)
add_test(NAME taskflow_all_unit COMMAND taskflow_tests)

# Utility target to run all tests
add_custom_target(taskflow_run_tests
  COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
  DEPENDS taskflow_tests
  COMMENT "Running all tests"
)
