message(STATUS "Building unit tests")
if(NOT TARGET googletest)
    include(FetchContent)
    FetchContent_Declare(
        googletest
        URL https://github.com/google/googletest/archive/refs/tags/v1.14.0.tar.gz
    )
    set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)
    set(BUILD_GMOCK OFF CACHE BOOL "" FORCE)
    set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
    FetchContent_MakeAvailable(googletest)
endif()

enable_testing()
include(GoogleTest)

file(GLOB_RECURSE TEST_MODULES "*.cpp")

message("TEST_SOURCE=${TEST_SOURCE}")

foreach(TEST_SOURCE ${TEST_MODULES})
    get_filename_component(TEST_NAME ${TEST_SOURCE} NAME_WE)
    add_executable(${TEST_NAME} ${TEST_SOURCE})
    target_link_libraries(${TEST_NAME} PRIVATE GTest::gtest_main TaskFlow::TaskFlow)
    # Static libtaskflow embeds sqlite_state_storage.o when TASKFLOW_WITH_SQLITE is ON; the linker
    # must see SQLite after the archive to resolve sqlite3_* symbols (transitive PUBLIC is not enough).
    if(TASKFLOW_WITH_SQLITE)
        target_link_libraries(${TEST_NAME} PRIVATE SQLite::SQLite3)
    endif()
    set_target_properties(${TEST_NAME} PROPERTIES FOLDER "tests")
    
    gtest_discover_tests(${TEST_NAME}
        TEST_PREFIX "${TEST_NAME}."
        PROPERTIES DISCOVERY_TIMEOUT 10
    )
    if(MEMPOOL_ENABLE_COVERAGE AND (CMAKE_CXX_COMPILER_ID MATCHES "GNU|Clang"))
        target_compile_options(${TEST_NAME} PRIVATE --coverage -fprofile-arcs -ftest-coverage)
        target_link_options(${TEST_NAME} PRIVATE --coverage)
    endif()
endforeach()

add_custom_target(run_all_tests
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure
    COMMENT "Running all colorcpp test modules"
)
