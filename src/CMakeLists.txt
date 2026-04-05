file(GLOB_RECURSE TASKFLOW_SOURCES "*.cpp")

if(NOT TASKFLOW_WITH_SQLITE)
  list(REMOVE_ITEM TASKFLOW_SOURCES storage/sqlite_state_storage.cpp)
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
