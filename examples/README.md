# Examples

Build with `-DTASKFLOW_BUILD_EXAMPLES=ON` (enabled by default) to compile examples.

## Numbered Examples (English documentation)

| Target | Description |
|--------|-------------|
| `01_minimal_task` | Minimal single task execution |
| `02_sequential_dag` | Linear DAG execution (A→B→C) with dependency management |
| `03_parallel_tasks` | Fan-out/fan-in parallel execution with thread pool |
| `04_conditional_edges` | Dynamic branching with conditional edges and runtime context |
| `05_task_context` | Shared data bus with task_ctx for lightweight data passing |
| `06_task_results` | Formal result pipelines with result_storage and get_result/set_result |
| `07_retry_policy` | Configurable retry behavior with exponential backoff |
| `08_cancellation` | Graceful cancellation with cooperative task checking |
| `09_observer_logger` | Custom observer implementation for logging and monitoring |
| `10_metrics_collection` | Performance metrics aggregation with metrics_observer |
| `11_sqlite_persistence` | Execution state persistence with SQLite storage |
| `12_blueprint_serialization` | Workflow blueprint serialization to JSON/binary |
| `13_async_execution` | Asynchronous execution with std::future and concurrent workflows |
| `14_custom_executor` | Custom parallel_executor implementations (inline, thread pool) |
| `15_event_hooks` | FFI-friendly event hooks for external system integration |

## Scenario examples (18–21)

| Target | Description |
|--------|-------------|
| `18_microservice_orchestration` | E-commerce order flow: two blueprints (in-stock vs procurement), payment retry, `memory_result_storage`, optional SQLite snapshots, async notify blueprint |
| `19_data_pipeline` | Log-style pipeline: fan-out ingest, cleanse/aggregate, `set_result` sink |
| `20_approval_workflow` | Approval chain with simulated timeout/escalation and `audit_log` history |
| `21_iot_event_processing` | IoT-style telemetry: two linear blueprints (critical vs log-only), async push notify |

## Other Examples

- `minimal_orchestrator` - Complete workflow example with task registration, DAG, observer, and result mapping
- `cancellation_example` - Alternative cancellation and progress demonstration

When SQLite is enabled, examples will link against `SQLite::SQLite3` (consistent with static library linking order).

## Building and Running

```bash
# Build all examples
mkdir -p build && cd build
cmake .. -DTASKFLOW_BUILD_EXAMPLES=ON
make examples

# Run specific example
./01_minimal_task
./02_sequential_dag
# etc.
```

## Notes

- All examples are now fully documented in English
- Each example demonstrates production-ready patterns and best practices
- Examples include error handling, validation, and proper resource management
- SQLite examples require `-DTASKFLOW_WITH_SQLITE=ON` build flag
