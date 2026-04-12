add_library(taskflow_c taskflow_c.cpp taskflow_c.h)

set_target_properties(taskflow_c PROPERTIES
  POSITION_INDEPENDENT_CODE ON
)

add_library(TaskFlow::TaskFlowC ALIAS taskflow_c)


target_include_directories(taskflow_c PUBLIC
  $<BUILD_INTERFACE:${CMAKE_CURRENT_SOURCE_DIR}>
  $<INSTALL_INTERFACE:include>
)

target_link_libraries(taskflow_c PUBLIC TaskFlow::TaskFlow)
target_compile_definitions(taskflow_c PRIVATE TASKFLOW_CAPI_VERSION_STRING="${PROJECT_VERSION}")
