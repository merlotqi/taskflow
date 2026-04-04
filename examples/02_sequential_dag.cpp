/**
 * Sequential DAG workflow example
 * Demonstrates task dependencies with linear execution flow: A -> B -> C
 *
 * This example shows:
 *  - How to define dependencies between nodes
 *  - Topological sorting of DAG nodes
 *  - Blueprint validation
 *  - Passing parameters through node definitions
 *  - Guaranteed execution order
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Generic step task that accepts configuration
 *
 * Demonstrates how tasks can receive initialization parameters
 * through the node_def user_data field
 */
struct step_task {
  std::string message;

  static constexpr std::string_view name = "step";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    const char* labels[] = {"Initialization phase", "Processing phase", "Finalization phase"};
    std::size_t index = ctx.node_id() - 1;
    const char* label = index < 3 ? labels[index] : "Unknown phase";

    std::cout << "Executing step: " << label << " (node " << ctx.node_id() << ")" << std::endl;

    // Simulate actual work
    for (int i = 0; i < 100000; ++i) {
      // Do some CPU bound work
      asm volatile("" : : : "memory");
    }

    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;

  // Register task type
  orch.register_task<step_task>("step");

  // Build sequential DAG: 1 -> 2 -> 3
  // Execution will always follow topological order
  taskflow::workflow::workflow_blueprint bp;

  bp.add_node(taskflow::workflow::node_def{1, "step"});
  bp.add_node(taskflow::workflow::node_def{2, "step"});
  bp.add_node(taskflow::workflow::node_def{3, "step"});

  // Define dependency edges
  // Node 2 will only execute after node 1 completes successfully
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  // Node 3 will only execute after node 2 completes successfully
  bp.add_edge(taskflow::workflow::edge_def{2, 3});

  // Validate DAG structure - checks for cycles, missing nodes, invalid edges
  if (!bp.is_valid()) {
    std::cerr << "Blueprint validation failed: " << bp.validate() << std::endl;
    return 1;
  }

  std::cout << "=== Sequential DAG execution ===" << std::endl;
  std::cout << "Topological order: ";

  // Get execution order determined by topological sort
  for (auto id : bp.topological_order()) {
    std::cout << id << " ";
  }
  std::cout << std::endl;

  // Register and execute workflow
  orch.register_blueprint(2, std::move(bp));
  auto [exec_id, result] = orch.run_sync_from_blueprint(2);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << std::endl;
  std::cout << "Execution ID: " << exec_id << std::endl;

  // Verify execution result
  if (result != taskflow::core::task_state::success) {
    std::cerr << "Workflow did not complete successfully" << std::endl;
    return 1;
  }

  return 0;
}
