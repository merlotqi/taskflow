/**
 * Workflow serialization example
 * Demonstrates blueprint serialization to JSON and binary formats
 *
 * This example shows:
 *  - How to serialize workflow_blueprint to JSON
 *  - How to deserialize from JSON back to blueprint
 *  - Binary serialization for compact storage
 *  - Version compatibility considerations
 *  - Limitations of condition function serialization
 *
 * Important notes:
 *  - Edge conditions (std::function<bool(const task_ctx&)>) cannot be serialized
 *  - Only static topology (nodes and unconditional edges) is preserved
 *  - Task factories must be registered after deserialization
 *  - Binary format is more compact but less human-readable
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Simple no-op task for demonstration
 */
struct noop_task {
  static constexpr std::string_view name = "noop";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    return taskflow::core::task_state::success;
  }
};

int main() {
  // Create workflow blueprint with nodes and edges
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "noop", "Start node"});
  bp.add_node(taskflow::workflow::node_def{2, "noop", "End node"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  // Serialize to JSON (human-readable)
  const std::string json = taskflow::workflow::serializer::to_json(bp);
  std::cout << "=== JSON Serialization ===\n" << json << "\n";

  // Deserialize from JSON
  auto restored = taskflow::workflow::serializer::from_json(json);
  if (!restored) {
    std::cerr << "Failed to deserialize from JSON\n";
    return 1;
  }

  // Serialize to binary (compact)
  auto binary = taskflow::workflow::serializer::to_binary(*restored);
  auto from_binary = taskflow::workflow::serializer::from_binary(binary);
  if (!from_binary) {
    std::cerr << "Failed to deserialize from binary\n";
    return 1;
  }

  // Register task and execute deserialized blueprint
  taskflow::engine::orchestrator orch;
  orch.register_task<noop_task>("noop");
  orch.register_blueprint(1, std::move(*from_binary));

  std::cout << "\n=== Running from serialized data ===\n";
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  return 0;
}
