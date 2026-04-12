# TaskFlow (orchestrator branch)

C++17 **workflow orchestration** library: blueprint (DAG) → runtime execution with a shared `TaskCtx` data bus, optional in-memory persistence, and hooks for observers. This branch (`feature/orchestrator`) replaces the earlier thread-pool `TaskManager` API with `tf::Orchestrator` and related types under `include/taskflow/`.

Design intent aligns with the in-repo notes [`taskcore`](taskcore) and [`doc.md`](doc.md): MQ and SQLite backends are planned extensions; the current MVP ships **memory storage**, **JSON blueprint serialization**, and **synchronous `run_sync`**.

## Layout

- `include/taskflow/core/` — IDs, `TaskState`, `TaskCtx`, `TaskBase`
- `include/taskflow/workflow/` — `WorkflowBlueprint`, nodes/edges, serializer
- `include/taskflow/engine/` — `Orchestrator`, `Scheduler`, `Executor`, `WorkflowExecution`, `TaskRegistry` / `itask_registry`
- `include/taskflow/storage/` — `StateStorage`, `MemoryStateStorage`
- `include/taskflow/observer/` — `Observer` callbacks
- `tests/` — GoogleTest
- `benchmarks/` — Google Benchmark
- `examples/minimal_orchestrator.cpp` — smallest runnable demo

Public umbrella header: `#include <taskflow/taskflow.hpp>`.

## Build

```bash
cmake -S . -B build -DTASKFLOW_BUILD_TESTS=ON -DTASKFLOW_BUILD_BENCHMARKS=ON -DTASKFLOW_BUILD_EXAMPLES=ON
# Optional: -DTASKFLOW_CXX20=ON (whole tree as C++20; see “C++20 (optional)” below)
cmake --build build -j
cd build && ctest --output-on-failure
./minimal_orchestrator
```

CMake target: `taskflow`. Aliases: `TaskFlow::taskflow` and `TaskFlow::TaskFlow`.

## Dependencies

- Threads
- [nlohmann/json](https://github.com/nlohmann/json) (fetched automatically via CMake FetchContent if `find_package` does not find it)
- GoogleTest / Google Benchmark (fetched when tests/benchmarks are enabled)
- Optional: SQLite 3 when configuring with `-DTASKFLOW_WITH_SQLITE=ON` (enables `taskflow::storage::sqlite_state_storage`). Use `taskflow::storage::state_storage_factory::create("sqlite", path)` for creation; unknown backends or constructor failures fall back to `memory_state_storage`.

## Observer and storage

- `orchestrator::add_observer` stores **raw pointers**; observers must outlive the orchestrator or be removed before destruction.
- `set_event_hooks` stores `integration::workflow_event_hooks`; during `run_sync` those callbacks are driven through a `obs::hooks_observer` adapter so the engine uses a **single observer list** for per-node execution (see `obs/hooks_observer.hpp`). You can also register a `hooks_observer` yourself if you manage its lifetime.
- `set_audit_log` records state transitions on `workflow_execution`.
- `taskflow/capi/taskflow_c.h` exposes the standalone **C ABI** used by future language bindings.

## ADL, `task_ctx` values, and threading

- Custom types stored in `task_ctx::set` / `get` need **`to_task_value(const T&) -> std::any`** and **`from_task_value(const std::any&, T&) -> bool`** as **non-member functions in the same namespace as `T`** (ADL). Defining them at global scope while `T` lives in `my_ns` will not be found.
- Optional explicit ADL entry points: **`taskflow::cpo::adl_to_task_value`** / **`adl_from_task_value`** in `taskflow/core/cpo.hpp` (same lookup rules, single named call site).
- **`task_ctx::data()`** returns a reference to the internal map **without** taking `data_mutex_`. Concurrent `set`/`get` (which lock) vs iteration or mutation through that reference is a **data race**. Use only on a single thread, or after the run finishes, or stick to `set`/`get`/`contains`.
- **`workflow_execution::node_states()`** returns the map **without** `state_mutex_`. Concurrent updates from workers vs unsynchronized reads/iteration is unsafe. Prefer **`get_node_state(node_id)`** for a consistent snapshot of one node.

## Blueprint JSON vs runtime edges

- `serializer::to_json` / `from_json` persist **from** / **to** only. **`edge_def::condition`** is a `std::function` and is **not** serialized; round-tripped blueprints lose conditional routing. Build conditions in code after load, or treat JSON as a **structural** blueprint only.

## C++20 (optional)

- Configure with `-DTASKFLOW_CXX20=ON` to build the whole tree as **C++20**. `task_wrapper` then uses the **`task_callable`** concept from `taskflow/core/task_concepts.hpp` instead of `std::enable_if_t<is_task_v<...>>`. The default remains **C++17**.

## Note on naming

This repository name overlaps the well-known [taskflow](https://github.com/taskflow/taskflow) parallel CPU library; this fork/branch is a **different** product (persistent-style orchestration). Rename the install target or namespace if you publish both.
