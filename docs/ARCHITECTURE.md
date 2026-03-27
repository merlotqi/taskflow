# TaskFlow architecture

This document describes how the library is structured and how control flows at runtime. Public API details live in the headers under `include/taskflow/` (see comments there).

## Components

| Piece | Role |
|--------|------|
| **TaskManager** | Singleton: assigns `TaskID`, owns `StateStorage`, `ThreadPool`, result storage, cancellation flags, and persistent task handles. |
| **Task** (type) | Any type `T` with `is_task_v<T>`: `T&` can be called as `task(ctx)` with `TaskCtx&`. Usually a lambda or functor. |
| **TaskCtx** | What user code receives inside `task(ctx)`: transition state, report progress, attach results, read cancellation. |
| **TaskRuntimeCtx** | Minimal context passed into the type-erased runner (`AnyTask`); expanded to `TaskCtx` when executing. |
| **AnyTask** | Type-erased wrapper: stores the callable on the heap and invokes `execute(Task&, TaskRuntimeCtx&, ResultStorage*)`. |
| **StateStorage** | Per-`TaskID` maps: `TaskState`, last update time, progress (`std::any`), error string, optional `ResultLocator`. Thread-safe (`shared_mutex`). |
| **ResultStorage** / **SimpleResultStorage** | Stores `ResultPayload` keyed by `ResultLocator`; used by `success_with_result` / `failure_with_result`. |
| **ThreadPool** | Fixed worker threads; `execute` queues fire-and-forget jobs (used for each task run). |

## Task lifecycle

Typical state sequence: `created` → `running` → `success` or `failure`.

- `submit_task` sets `created` immediately, then (if `start_processing` was called) queues work on the pool.
- The executor calls `ctx.begin()` (`running`), runs `task(ctx)`, then if the task did not set a terminal state, applies default completion: `success()` or `failure("cancelled")` depending on cancellation (see `execute_*` in `task_manager.hpp`).

## Disposable vs persistent

- **Disposable**: the callable is moved into the worker closure (or equivalent); only state/result metadata remain after completion.
- **Persistent**: the callable is stored in `persistent_tasks_`. The first run and `reawaken_task` both execute that stored `AnyTask` under a shared lock, so the body is never rebuilt from an already-moved parameter.

## Traits and `execute` dispatch

`task_traits<T>` supplies `cancellable` and `TaskObservability observability`. The primary template also reads `T::cancellable` and `T::observability` when those `static constexpr` members exist.

`execute` picks among `execute_task`, `execute_cancellable_task`, and `execute_observable_task` based on `is_cancellable_task_v` and observable traits. All paths still update `StateStorage` through `TaskCtx` unless you specialize behavior elsewhere.

## Cancellation

`cancel_task(id)` records a flag. `TaskCtx::is_cancelled()` consults it. Cancellable task types should poll and exit or set `failure` explicitly; if the task returns without a terminal state while cancelled, the runtime sets `failure("cancelled")`.

## Cleanup

`cleanup_completed_tasks(max_age)` (also triggered periodically from a background thread):

1. `StateStorage::cleanup_old_tasks` removes stale rows by timestamp and returns removed ids plus optional `ResultLocator`.
2. `TaskManager` removes matching entries from result storage, `cancelled_tasks_`, and `persistent_tasks_`.

## Threading notes

- External callers can query state/progress/results concurrently with workers; internal maps use appropriate locking.
- `ThreadPool::execute` catches exceptions from queued work and logs them; task bodies are still wrapped in `try/catch` inside `execute_*` so `TaskCtx::failure` runs for task exceptions.

## Further reading

- [examples/README.md](../examples/README.md) — runnable programs and CTest names.
- [CONTRIBUTING.md](../CONTRIBUTING.md) — build, test, CI.
