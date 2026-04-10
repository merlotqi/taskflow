find_package(pybind11 CONFIG QUIET)
if(pybind11_FOUND)
  message(STATUS "Found System pybind11: ${pybind11_VERSION}")
else()
  message(STATUS "System pybind11 not found, downloading via FetchContent...")
  include(FetchContent)

  FetchContent_Declare(
      pybind11
      GIT_REPOSITORY https://github.com/pybind/pybind11.git
      GIT_TAG v2.13.6
      GIT_SHALLOW TRUE
  )
    
  FetchContent_MakeAvailable(pybind11)
endif()

pybind11_add_module(taskflow_python src/python_binding.cpp)

set_target_properties(taskflow_python PROPERTIES
  OUTPUT_NAME taskflow
)

target_link_libraries(taskflow_python PRIVATE 
  TaskFlow::TaskFlowC
)
