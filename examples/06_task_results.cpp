/**
 * Task Results example
 * Demonstrates result_collector / result_storage for formal result pipelines
 *
 * This example shows:
 *  - How to write results using ctx.set_result()
 *  - How to read results using ctx.get_result()
 *  - Result storage isolation by (exec_id, node_id, key)
 *  - MemoryResultStorage vs StateStorage differences
 *  - Result aggregation patterns
 *
 * Task results are stored in result_storage and are:
 *  - Immutable after being set
 *  - Accessible to downstream nodes
 *  - Isolated per execution instance
 *  - Suitable for formal result pipelines
 */

#include <iostream>
#include <memory>
#include <taskflow/storage/memory_result_storage.hpp>
#include <taskflow/taskflow.hpp>

/**
 * Task that writes computation result to result storage
 */
struct write_result_task {
  static constexpr std::string_view name = "write_result";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    // Store computation result with key "total"
    ctx.set_result("total", static_cast<int64_t>(42));

    std::cout << "Node " << ctx.node_id() << " wrote result: total=42\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Task that reads result from previous node
 */
struct read_result_task {
  static constexpr std::string_view name = "read_result";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    // Get result from node 1 with key "total"
    auto result = ctx.get_result<int64_t>(1, "total");

    if (result) {
      std::cout << "Node " << ctx.node_id() << " read result from node 1: total=" << *result << "\n";
    } else {
      std::cout << "Node " << ctx.node_id() << " could not find result from node 1\n";
    }

    return taskflow::core::task_state::success;
  }
};

int main() {
  // Create result storage for this execution
  auto result_storage = std::make_unique<taskflow::storage::memory_result_storage>();
  taskflow::engine::orchestrator orch(std::move(result_storage));

  orch.register_task<write_result_task>("write_result");
  orch.register_task<read_result_task>("read_result");

  // Build workflow: write_result -> read_result
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "write_result"});
  bp.add_node(taskflow::workflow::node_def{2, "read_result"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  if (!bp.is_valid()) {
    std::cerr << "Invalid blueprint: " << bp.validate() << std::endl;
    return 1;
  }

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== set_result / get_result with result_storage ===\n";
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  // Verify result storage contents
  if (const auto* exec = orch.get_execution(exec_id)) {
    auto stored_result = exec->context().get_result<int64_t>(1, "total");
    if (stored_result) {
      std::cout << "Result storage verified: total = " << *stored_result << "\n";
    }
  }

  return 0;
}
