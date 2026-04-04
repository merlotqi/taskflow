/**
 * SQLite persistence example
 * Demonstrates execution state persistence with SQLite storage
 *
 * This example shows:
 *  - How to configure SQLite state storage with orchestrator
 *  - Automatic execution snapshot persistence
 *  - Execution state recovery and inspection
 *  - Database cleanup and management
 *  - Cross-session execution persistence
 *
 * SQLite storage is optional and requires:
 *  - CMake flag: -DTASKFLOW_WITH_SQLITE=ON
 *  - SQLite3 development libraries
 */

#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <taskflow/taskflow.hpp>

#if defined(TASKFLOW_HAS_SQLITE)
#include <taskflow/storage/sqlite_state_storage.hpp>
#endif

/**
 * Task that writes checkpoint data to execution context
 */
struct persist_task {
  static constexpr std::string_view name = "persist";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("checkpoint", std::string("ok"));
    ctx.set("timestamp", std::chrono::system_clock::now().time_since_epoch().count());
    return taskflow::core::task_state::success;
  }
};

int main() {
#if !defined(TASKFLOW_HAS_SQLITE)
  std::cout << "This example requires SQLite support (build with -DTASKFLOW_WITH_SQLITE=ON)\n";
  return 0;
#else
  // Use temporary directory if available, otherwise current directory
  std::string db_path = "taskflow_example_state.db";
  if (const char* tmp_dir = std::getenv("TMPDIR")) {
    db_path = std::string(tmp_dir) + "/taskflow_example_state.db";
  }

  // Create SQLite state storage
  auto storage = std::make_unique<taskflow::storage::sqlite_state_storage>(db_path);
  taskflow::engine::orchestrator orch(std::move(storage));

  orch.register_task<persist_task>("persist");

  // Build workflow with multiple nodes
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "persist"});
  bp.add_node(taskflow::workflow::node_def{2, "persist"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== SQLite persistence (db=" << db_path << ") ===\n";
  std::cout << "Workflow will be automatically persisted after each step\n\n";

  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution completed: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  // Read back persisted data
  taskflow::storage::sqlite_state_storage reader(db_path);
  auto all_ids = reader.list_all();

  std::cout << "\nDatabase contains " << all_ids.size() << " execution(s):\n";
  for (auto id : all_ids) {
    std::cout << "  Execution ID: " << id << "\n";
  }

  // Load and inspect specific execution
  if (auto snapshot = reader.load(exec_id)) {
    std::cout << "\nSnapshot details for execution " << exec_id << ":\n";
    std::cout << "  Size: " << snapshot->size() << " bytes\n";

    // Show preview of snapshot data
    const std::size_t preview_size = std::min<std::size_t>(200, snapshot->size());
    std::string_view preview(snapshot->data(), preview_size);
    std::cout << "  Preview: " << preview << "...\n";
  }

  // Clean up - remove execution from database
  reader.remove(exec_id);
  std::cout << "\nExecution " << exec_id << " removed from database\n";

  return 0;
#endif
}
