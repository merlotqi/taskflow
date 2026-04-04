include(FetchContent)

FetchContent_Declare(
  googlebenchmark
  URL https://github.com/google/benchmark/archive/refs/tags/v1.8.3.zip
  DOWNLOAD_EXTRACT_TIMESTAMP TRUE
)
set(BENCHMARK_ENABLE_TESTING OFF CACHE BOOL "" FORCE)
FetchContent_MakeAvailable(googlebenchmark)

add_executable(taskflow_benchmark
  orchestrator_benchmark.cpp
)

target_link_libraries(taskflow_benchmark PRIVATE taskflow benchmark::benchmark_main)
if(TASKFLOW_WITH_SQLITE)
  target_link_libraries(taskflow_benchmark PRIVATE SQLite::SQLite3)
endif()

# Utility target to run all benchmarks
add_custom_target(taskflow_run_benchmarks
  COMMAND taskflow_benchmark --benchmark_format=json --benchmark_out=benchmark_results.json
  DEPENDS taskflow_benchmark
  COMMENT "Running all benchmarks"
)
