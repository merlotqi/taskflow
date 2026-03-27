# TaskFlow

**TaskFlow** is a lightweight, high-performance, and thread-safe C++17 asynchronous task management library. It provides a simple yet powerful API for submitting and managing asynchronous tasks with progress tracking, cancellation, and error handling.

## 🚀 Key Features

* **Simple API**: Submit tasks as lambda functions or callable objects
* **Flexible Observability**: Choose between no observation, basic state tracking, or full progress reporting
* **Cancellation Support**: Tasks can check for cancellation and handle it gracefully
* **Error Handling**: Comprehensive error reporting and state management
* **Persistent Tasks**: Reusable tasks that can be reawakened with new parameters
* **Thread-Safe**: Designed for concurrent access from multiple threads
* **C++17**: Modern C++ with concepts and constexpr where available
* **Cross-Platform**: Works on Windows, Linux, and macOS

---

## 🏗 Architecture

1. **TaskManager**: The main singleton that manages task submission and execution
2. **TaskCtx**: Context object passed to tasks for state management and progress reporting
3. **StateStorage**: Internal storage for task states, progress, and errors
4. **Thread Pool**: Manages worker threads for task execution

Longer design notes, lifecycle, and threading: [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) and [docs/README.md](docs/README.md).

---

## 💻 Quick Start

### 1. Include the Header

```cpp
#include <taskflow/task_manager.hpp>
```

### 2. Submit a Task

```cpp
// Get the task manager instance
auto& manager = taskflow::TaskManager::getInstance();

// Start processing (specify number of threads)
manager.start_processing(4);

// Submit a simple task
auto task_id = manager.submit_task([](taskflow::TaskCtx& ctx) {
    std::cout << "Task " << ctx.id << " is running" << std::endl;
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    ctx.success();  // Mark as successful
});
```

### 3. Monitor Task State

```cpp
// Query task state
auto state = manager.query_state(task_id);
if (state) {
    std::cout << "Task state: " << static_cast<int>(*state) << std::endl;
}
```

### 4. Task with Progress

```cpp
auto progress_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    for (int i = 0; i <= 100; i += 25) {
        ctx.report_progress(static_cast<float>(i) / 100.0f, "Step " + std::to_string(i));
        std::this_thread::sleep_for(std::chrono::milliseconds(50));
    }
    ctx.success();
});

// Monitor progress
if (auto progress = manager.get_progress(progress_task)) {
    std::cout << "Progress: " << progress->progress * 100.0f << "% - " << progress->message << std::endl;
}
```

### 5. Handle Errors

```cpp
auto failing_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    try {
        // Some work that might fail
        throw std::runtime_error("Something went wrong");
    } catch (const std::exception& e) {
        ctx.failure(e.what());
    }
});

// Check for errors
if (auto error = manager.get_error(failing_task)) {
    std::cout << "Error: " << *error << std::endl;
}
```

### 6. Cancellation

```cpp
auto cancellable_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    while (!ctx.is_cancelled()) {
        // Do work, check for cancellation periodically
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
    if (ctx.is_cancelled()) {
        ctx.failure("Task was cancelled");
    } else {
        ctx.success();
    }
});

// Cancel the task
manager.cancel_task(cancellable_task);
```

### 7. Task Results

Tasks can now store execution results that persist beyond completion:

```cpp
auto result_task = manager.submit_task([](taskflow::TaskCtx& ctx) {
    // Process data and create result
    nlohmann::json result = {
        {"processed_items", 42},
        {"success_rate", 0.95},
        {"output", "processed data"}
    };

    // Store result with task completion
    ctx.success_with_result(taskflow::ResultPayload::json(result));
});

// Retrieve result after completion
if (auto result = manager.get_result(result_task)) {
    if (result->kind == taskflow::ResultKind::json) {
        std::cout << "Result: " << result->data.json_data.dump() << std::endl;
    }
}
```

### 8. Persistent Tasks

Persistent tasks can be reawakened with new parameters after completion:

```cpp
// Submit a persistent task
auto persistent_id = manager.submit_task([](taskflow::TaskCtx& ctx) {
    std::cout << "Initial execution" << std::endl;
    ctx.success();
}, taskflow::TaskLifecycle::persistent);

// Check if it's persistent
if (manager.is_persistent_task(persistent_id)) {
    // Reawaken with new logic
    manager.reawaken_task(persistent_id, [](taskflow::TaskCtx& ctx) {
        std::cout << "Reawakened with new logic!" << std::endl;
        ctx.success();
    });
}
```

---

## 🔧 Task Traits Configuration

Execution and dispatch use `task_traits<TaskType>`. The primary template reads optional capability fields from your task type when present:

- `static constexpr taskflow::TaskObservability observability` — defaults to `basic` if omitted
- `static constexpr bool cancellable` — defaults to `false` if omitted

You can still **fully specialize** `task_traits<MyTask>` to override name, description, priority, and capabilities in one place.

```cpp
// Callable type with type-level traits (picked up by default task_traits<T>)
struct MyProgressTask {
    static constexpr taskflow::TaskObservability observability = taskflow::TaskObservability::progress;
    static constexpr bool cancellable = true;

    void operator()(taskflow::TaskCtx& ctx) const { ctx.success(); }
};

MyProgressTask my_task;
auto task_id = manager.submit_task(my_task);
```

### Available Task Capabilities

- **Observability Levels**:
  - `TaskObservability::none`: Dispatch may match the same path as other tasks; task state in `StateStorage` is still updated for typical executions (use `task_traits` / execution path for exact behavior).
  - `TaskObservability::basic`: Basic state observation (start/end)
  - `TaskObservability::progress`: Progress reporting via `TaskCtx::report_progress`

- **Cancellation**: Set `cancellable = true` so the runtime uses the cancellable execution path; if the task returns without setting a terminal state and cancellation was requested, the task is marked failed with message `"cancelled"`.

- **Lifecycle**: Choose between `TaskLifecycle::disposable` and `TaskLifecycle::persistent`

---

## ⚙️ Configuration

The TaskManager automatically manages cleanup of completed tasks:

* **Cleanup Interval**: Runs every 30 minutes by default
* **Max Task Age**: Tasks older than 24 hours are automatically cleaned up
* **Thread Count**: Specify number of worker threads when calling `start_processing()`
* **What cleanup removes**: For each expired task id, `cleanup_completed_tasks` drops state/progress/error/locator rows, removes the associated entry from result storage (when a locator was present), clears cancellation flags, and removes persistent task handles so maps stay consistent.

**Manual regression checks** (after changing execution or storage): configure and build with examples enabled (`cmake -S . -B build -DTASKFLOW_BUILD_EXAMPLES=ON`), then run `cmake --build build --target taskflow_run_examples` (runs all examples via CTest), or spot-check `./build/persistent_task` and `./build/task_with_result`. See [examples/README.md](examples/README.md).

## 🛠 Dependencies

* [nlohmann/json](https://github.com/nlohmann/json): For internal data handling
* **C++17** or higher
