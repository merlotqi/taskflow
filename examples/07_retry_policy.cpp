/**
 * Retry policy example
 * Demonstrates configurable retry behavior with exponential backoff
 *
 * This example shows:
 *  - How to configure retry_policy on node_def
 *  - Exponential backoff calculation
 *  - Retry attempt tracking in task_ctx
 *  - Custom should_retry conditions
 *  - Retry timeout and max_attempts limits
 *
 * Retry policies are configured per node and control:
 *  - max_attempts: maximum number of retry attempts
 *  - initial_delay: delay before first retry
 *  - backoff_multiplier: exponential backoff factor
 *  - should_retry: optional condition to determine retry eligibility
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Task that simulates flaky behavior with retry tracking
 */
struct flaky_task {
  static constexpr std::string_view name = "flaky";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    // Track retry attempts in shared context
    auto attempt = ctx.get<int64_t>("_retry_attempts").value_or(0);
    ++attempt;
    ctx.set("_retry_attempts", attempt);

    std::cout << "Flaky task attempt " << attempt << " (node=" << ctx.node_id() << ")\n";

    // Simulate failure for first 2 attempts, success on 3rd
    if (attempt < 3) {
      return taskflow::core::task_state::failed;
    }

    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task<flaky_task>("flaky");

  // Build workflow with retry policy configuration
  taskflow::workflow::workflow_blueprint bp;

  // Configure node with retry policy
  taskflow::workflow::node_def node{1, "flaky", "Flaky task with retry"};
  node.retry = taskflow::core::retry_policy{};

  // Configure retry parameters
  node.retry->max_attempts = 5;                               // Maximum 5 attempts
  node.retry->initial_delay = std::chrono::milliseconds(20);  // 20ms initial delay
  node.retry->backoff_multiplier = 1.5f;                      // 1.5x exponential backoff

  bp.add_node(std::move(node));

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Retry until success ===\n";
  std::cout << "Retry configuration:\n";
  std::cout << "  max_attempts: 5\n";
  std::cout << "  initial_delay: 20ms\n";
  std::cout << "  backoff_multiplier: 1.5x\n";
  std::cout << "  Expected attempts: 3 (success on 3rd)\n\n";

  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  // Show retry statistics
  if (const auto* exec = orch.get_execution(exec_id)) {
    auto final_attempts = exec->context().get<int64_t>("_retry_attempts");
    if (final_attempts) {
      std::cout << "Total retry attempts: " << *final_attempts << "\n";
    }
  }

  return 0;
}
