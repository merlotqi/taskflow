execute_process(
  COMMAND node -p "require('node-addon-api').include_dir"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE NODE_ADDON_API_RESULT
  OUTPUT_VARIABLE NODE_ADDON_API_INCLUDE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT NODE_ADDON_API_RESULT EQUAL 0)
  message(FATAL_ERROR
    "Failed to resolve node-addon-api from ${CMAKE_CURRENT_SOURCE_DIR}. "
    "Run `npm install` in the node/ directory or configure with -DTASKFLOW_BUILD_NODE=OFF."
  )
endif()

set(NODE_ADDON_API_INCLUDE_DIR "${CMAKE_CURRENT_SOURCE_DIR}/${NODE_ADDON_API_INCLUDE_DIR}")

execute_process(
  COMMAND node -p "require('node:path').resolve(require('node:path').dirname(process.execPath), '..', 'include', 'node')"
  WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}
  RESULT_VARIABLE NODE_INCLUDE_RESULT
  OUTPUT_VARIABLE NODE_INCLUDE_DIR
  OUTPUT_STRIP_TRAILING_WHITESPACE
)

if(NOT NODE_INCLUDE_RESULT EQUAL 0 OR NOT EXISTS "${NODE_INCLUDE_DIR}/node_api.h")
  message(FATAL_ERROR
    "Failed to locate Node.js headers from the current Node runtime. "
    "Expected to find node_api.h under ${NODE_INCLUDE_DIR}."
  )
endif()

add_library(taskflow_node MODULE
  src/node_binding.cpp
)

target_include_directories(taskflow_node PRIVATE
  ${PROJECT_SOURCE_DIR}/include
  ${NODE_INCLUDE_DIR}
  ${NODE_ADDON_API_INCLUDE_DIR}
)

target_compile_definitions(taskflow_node PRIVATE
  NAPI_DISABLE_CPP_EXCEPTIONS
)

target_compile_features(taskflow_node PRIVATE cxx_std_17)

target_link_libraries(taskflow_node PRIVATE
  TaskFlow::TaskFlowC
)

set_target_properties(taskflow_node PROPERTIES
  PREFIX ""
  SUFFIX ".node"
  OUTPUT_NAME "Taskflow"
)

if(APPLE)
  target_link_options(taskflow_node PRIVATE "-undefined" "dynamic_lookup")
endif()
