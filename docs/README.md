# TaskFlow documentation (orchestrator branch)

- **[ARCHITECTURE.md](ARCHITECTURE.md)** — orchestrator stack (blueprint, execution, scheduler, storage).

## API reference

Headers under `include/taskflow/`:

| Header | Contents |
|--------|----------|
| `taskflow/taskflow.hpp` | Umbrella include for the public surface |
| `taskflow/engine/orchestrator.hpp` | `Orchestrator` |
| `taskflow/workflow/blueprint.hpp` | `WorkflowBlueprint`, `NodeDef`, `EdgeDef` |
| `taskflow/workflow/serializer.hpp` | JSON blueprint + execution snapshot helpers |
| `taskflow/core/task.hpp` | `TaskBase`, factories |
| `taskflow/core/task_ctx.hpp` | `TaskCtx` data bus |
| `taskflow/storage/memory_storage.hpp` | `MemoryStateStorage` |
