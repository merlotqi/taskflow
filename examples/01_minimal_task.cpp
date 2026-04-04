/**
 * Minimal single task example
 * Demonstrates the most basic TaskFlow usage pattern
 *
 * This example shows:
 *  - How to define a task type
 *  - How to register tasks with the orchestrator
 *  - How to create a simple workflow blueprint
 *  - How to execute a workflow synchronously
 *  - Basic error checking and result handling
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Simple hello world task implementation
 *
 * All tasks must implement:
 *  - static constexpr std::string_view name
 *  - operator() taking task_ctx& and returning task_state
 */
struct hello_task {
  static constexpr std::string_view name = "hello";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "Hello from TaskFlow!" << std::endl;
    std::cout << "  Execution ID: " << ctx.exec_id() << std::endl;
    std::cout << "  Node ID:      " << ctx.node_id() << std::endl;

    // Tasks should always return appropriate state:
    // success, failed, cancelled, skipped, pending
    return taskflow::core::task_state::success;
  }
};

int main() {
  // 1. Create orchestrator instance
  // Orchestrator is the main entry point for all workflow operations
  taskflow::engine::orchestrator orch;

  // 2. Register task type
  // Tasks must be registered before they can be referenced in blueprints
  orch.register_task<hello_task>("hello");

  // 3. Build workflow blueprint (single node)
  // Blueprint is the static definition of your workflow DAG
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "hello"});

  // Always validate blueprints before registration
  if (!bp.is_valid()) {
    std::cerr << "Blueprint validation failed: " << bp.validate() << std::endl;
    return 1;
  }

  // 4. Register blueprint and run execution
  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Running single task ===" << std::endl;
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << std::endl;

  // Always check execution result in production code
  if (result != taskflow::core::task_state::success) {
    std::cerr << "Workflow execution failed" << std::endl;
    return 1;
  }

  return 0;
}
