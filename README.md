# TaskFlow (orchestrator branch)

C++17 **workflow orchestration** library: blueprint (DAG) → runtime execution with a shared `TaskCtx` data bus, optional in-memory persistence, and hooks for observers. This branch (`feature/orchestrator`) replaces the earlier thread-pool `TaskManager` API with `tf::Orchestrator` and related types under `include/taskflow/`.

Design intent aligns with the in-repo notes [`taskcore`](taskcore) and [`doc.md`](doc.md): MQ and SQLite backends are planned extensions; the current MVP ships **memory storage**, **JSON blueprint serialization**, and **synchronous `run_sync`**.

## Layout

- `include/taskflow/core/` — IDs, `TaskState`, `TaskCtx`, `TaskBase`
- `include/taskflow/workflow/` — `WorkflowBlueprint`, nodes/edges, serializer
- `include/taskflow/engine/` — `Orchestrator`, `Scheduler`, `Executor`, `WorkflowExecution`, `TaskRegistry`
- `include/taskflow/storage/` — `StateStorage`, `MemoryStateStorage`
- `include/taskflow/observer/` — `Observer` callbacks
- `tests/` — GoogleTest
- `benchmarks/` — Google Benchmark
- `examples/minimal_orchestrator.cpp` — smallest runnable demo

Public umbrella header: `#include <taskflow/taskflow.hpp>`.

## Build

```bash
cmake -S . -B build -DTASKFLOW_BUILD_TESTS=ON -DTASKFLOW_BUILD_BENCHMARKS=ON -DTASKFLOW_BUILD_EXAMPLES=ON
cmake --build build -j
cd build && ctest --output-on-failure
./minimal_orchestrator
```

CMake target: `taskflow_core`. Aliases: `TaskFlow::taskflow_core` and `TaskFlow::TaskFlow`.

## Dependencies

- Threads
- [nlohmann/json](https://github.com/nlohmann/json) (fetched via CMake if not found)
- GoogleTest / Google Benchmark (fetched when tests/benchmarks are enabled)

## Note on naming

This repository name overlaps the well-known [taskflow](https://github.com/taskflow/taskflow) parallel CPU library; this fork/branch is a **different** product (persistent-style orchestration). Rename the install target or namespace if you publish both.
