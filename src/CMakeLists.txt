set(TASKFLOW_SOURCES
  core/audit_log.cpp
  core/task_ctx.cpp
  workflow/blueprint.cpp
  workflow/serializer.cpp
  engine/default_thread_pool.cpp
  engine/execution.cpp
  engine/execution_snapshot.cpp
  engine/registry.cpp
  engine/scheduler.cpp
  engine/executor.cpp
  engine/orchestrator.cpp
  observer/logger.cpp
  observer/metrics.cpp
  storage/memory_state_storage.cpp
  storage/memory_result_storage.cpp
)

if(TASKFLOW_WITH_SQLITE)
  list(APPEND TASKFLOW_SOURCES storage/sqlite_state_storage.cpp)
endif()

add_library(taskflow ${TASKFLOW_SOURCES})

add_library(TaskFlow::TaskFlow ALIAS taskflow)

target_include_directories(taskflow PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}/../include>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskflow PUBLIC Threads::Threads nlohmann_json::nlohmann_json)

if(TASKFLOW_WITH_SQLITE)
  target_compile_definitions(taskflow PUBLIC TASKFLOW_HAS_SQLITE=1)
  target_link_libraries(taskflow PUBLIC SQLite::SQLite3)
endif()
