# Architecture (orchestrator branch)

The previous **TaskManager / thread pool** design is **removed** on `feature/orchestrator`.

Current stack:

- **WorkflowBlueprint** — static DAG (nodes + edges), validated acyclic, topological order.
- **WorkflowExecution** — runtime: per-node `TaskState`, shared `TaskCtx` JSON bus, retry counters (reserved).
- **TaskRegistry** — maps `task_type` string to `TaskBase` factories.
- **Scheduler** — pending nodes whose inbound edges all succeeded.
- **Executor** — runs one node: `TaskBase::execute`, updates state, notifies `Observer`s.
- **Orchestrator** — creates executions, `run_sync` loop, optional `StateStorage` snapshots after steps.
- **MemoryStateStorage** — JSON blobs via `execution_to_json` / `execution_from_json`.

See the root [README.md](../README.md) and headers under `include/taskflow/` for API details.
