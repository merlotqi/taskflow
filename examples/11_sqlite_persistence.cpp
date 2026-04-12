/**
 * SQLite persistence example
 * Demonstrates execution state persistence via state_storage_factory (SQLite when available, else memory fallback)
 *
 * This example shows:
 *  - state_storage_factory::create("sqlite", path) with automatic fallback to memory
 *  - Automatic execution snapshot persistence via orchestrator
 *  - Listing / loading / removing using the same storage instance (orchestrator::state_storage)
 */

#include <algorithm>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <taskflow/taskflow.hpp>

struct persist_task {
  static constexpr std::string_view name = "persist";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("checkpoint", std::string("ok"));
    ctx.set("timestamp", std::chrono::system_clock::now().time_since_epoch().count());
    return taskflow::core::task_state::success;
  }
};

int main() {
  std::string db_path = "taskflow_example_state.db";
  if (const char* tmp_dir = std::getenv("TMPDIR")) {
    db_path = std::string(tmp_dir) + "/taskflow_example_state.db";
  }

  auto sr = taskflow::storage::state_storage_factory::create("sqlite", db_path);
  if (sr.fell_back_to_memory) {
    std::cout << "Note: sqlite backend unavailable; using in-memory state storage (no file persistence).\n";
  }

  auto storage = std::move(sr.storage);
  taskflow::engine::orchestrator orch(std::move(storage));

  orch.register_task<persist_task>("persist");

  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "persist"});
  bp.add_node(taskflow::workflow::node_def{2, "persist"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== State persistence (backend=" << sr.resolved_backend;
  if (sr.resolved_backend == "sqlite" && !sr.fell_back_to_memory) {
    std::cout << " db=" << db_path;
  }
  std::cout << ") ===\n";
  std::cout << "Workflow snapshots are saved after each step when state_storage is set.\n\n";

  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution completed: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  auto* store = orch.state_storage();
  if (!store) {
    std::cout << "No state storage configured.\n";
    return 0;
  }

  auto all_ids = store->list_all();
  std::cout << "\nStorage contains " << all_ids.size() << " execution(s):\n";
  for (auto id : all_ids) {
    std::cout << "  Execution ID: " << id << "\n";
  }

  if (auto snapshot = store->load(exec_id)) {
    std::cout << "\nSnapshot details for execution " << exec_id << ":\n";
    std::cout << "  Size: " << snapshot->size() << " bytes\n";

    const std::size_t preview_size = std::min<std::size_t>(200, snapshot->size());
    std::string_view preview(snapshot->data(), preview_size);
    std::cout << "  Preview: " << preview << "...\n";
  }

  store->remove(exec_id);
  std::cout << "\nExecution " << exec_id << " removed from storage\n";

  return 0;
}
