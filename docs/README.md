# TaskFlow documentation

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — runtime flow, main types, persistence, traits, cleanup, threading.

## API reference

There is no separate generated API site; treat the headers as the contract:

| Header | Contents |
|--------|----------|
| `taskflow/task_manager.hpp` | `TaskManager`, task execution dispatch (`execute`, `execute_*`) |
| `taskflow/task_ctx.hpp` | `TaskCtx`, `TaskRuntimeCtx` |
| `taskflow/task_traits.hpp` | Enums, `ResultPayload`, `ResultStorage`, `task_traits`, `is_task_v`, progress rules |
| `taskflow/state_storage.hpp` | `StateStorage` |
| `taskflow/any_task.hpp` | `AnyTask`, `make_any_task` |
| `taskflow/threadpool.hpp` | `ThreadPool` (global class used by `TaskManager`) |

Include `taskflow/task_manager.hpp` for typical applications; it pulls in the rest.
