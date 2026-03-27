# TaskFlow examples

Each source file builds a standalone executable (see root `CMakeLists.txt` and `cmake/TaskFlowExamples.cmake`).

| Executable | What it shows |
|------------|----------------|
| `basic_task_submission` | Submit a lambda, wait until finished. |
| `multiple_tasks` | Several concurrent tasks, `wait_all_inactive`. |
| `task_with_failure` | `ctx.failure` and `get_error`. |
| `task_with_progress` | Float/message progress and polling `get_progress`. |
| `task_with_result` | `success_with_result` with JSON and `get_result`. |
| `persistent_task` | `TaskLifecycle::persistent` and `reawaken_task`. |
| `task_traits_and_types` | Functor types in `task_types.hpp` (observability, cancel, custom progress). |

Shared helpers live in `example_util.hpp` (`print_banner`, `wait_until_inactive`, `wait_all_inactive`).

## Run all examples from the build tree

After configuring with examples enabled:

```bash
cmake --build build --target taskflow_run_examples
```

Or run CTest directly:

```bash
cd build && ctest --output-on-failure -R '^taskflow_example_'
```
