# TaskFlowExamples.cmake — register example executables, CTest entries, and taskflow_run_examples.
#
# Usage (from the TaskFlow project root CMakeLists.txt, after TaskFlow::TaskFlow exists):
#   include(cmake/TaskFlowExamples.cmake)
#   taskflow_register_examples(TaskFlow::TaskFlow)
#
# Relies on CMAKE_CURRENT_SOURCE_DIR pointing at the TaskFlow root (so examples/ resolves correctly).

macro(taskflow_register_examples _link_target)
  # Expect this file to be included from the TaskFlow project root CMakeLists.txt
  set(_TASKFLOW_EX_DIR "${CMAKE_CURRENT_SOURCE_DIR}/examples")

  set(TASKFLOW_EXAMPLE_TARGETS
    basic_task_submission
    multiple_tasks
    task_with_failure
    task_with_progress
    task_with_result
    persistent_task
    task_traits_and_types
  )

  foreach(_name IN LISTS TASKFLOW_EXAMPLE_TARGETS)
    add_executable(${_name} "${_TASKFLOW_EX_DIR}/${_name}.cpp")
    target_include_directories(${_name} PRIVATE "${_TASKFLOW_EX_DIR}")
    target_link_libraries(${_name} PRIVATE ${_link_target})
  endforeach()

  enable_testing()
  foreach(_name IN LISTS TASKFLOW_EXAMPLE_TARGETS)
    add_test(NAME taskflow_example_${_name} COMMAND ${_name})
    set_tests_properties(taskflow_example_${_name} PROPERTIES
      WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    )
  endforeach()

  add_custom_target(taskflow_run_examples
    COMMAND ${CMAKE_CTEST_COMMAND} --output-on-failure -R "^taskflow_example_"
    WORKING_DIRECTORY ${CMAKE_CURRENT_BINARY_DIR}
    COMMENT "Run all TaskFlow examples (CTest)"
  )
  add_dependencies(taskflow_run_examples ${TASKFLOW_EXAMPLE_TARGETS})
endmacro()
