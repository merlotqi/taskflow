/**
 * Runtime cancellation example
 * Demonstrates graceful workflow cancellation and cleanup
 *
 * This example shows:
 *  - How to execute workflows asynchronously with run_async()
 *  - How to cancel running executions with cancel_execution()
 *  - How tasks should check cancellation status with ctx.is_cancelled()
 *  - Proper cleanup and state reporting after cancellation
 *  - Progress reporting during long-running operations
 *
 * Cancellation is cooperative - tasks must periodically check
 * ctx.is_cancelled() and return task_state::cancelled when detected.
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

/**
 * Long-running task that supports cooperative cancellation
 */
struct long_task {
  static constexpr std::string_view name = "long_task";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "Long task started (node=" << ctx.node_id() << ")\n";

    // Simulate long-running operation in multiple steps
    for (int step = 0; step < 20; ++step) {
      // Check for cancellation at each step
      if (ctx.is_cancelled()) {
        std::cout << "Long task: cancellation detected at step " << step << "/20\n";
        return taskflow::core::task_state::cancelled;
      }

      // Simulate work for this step
      std::this_thread::sleep_for(std::chrono::milliseconds(100));

      // Report progress (optional)
      float progress = static_cast<float>(step + 1) / 20.0f;
      ctx.report_progress(progress);
      std::cout << "Long task: completed step " << step + 1 << "/20 (progress: " << (progress * 100) << "%)\n";
    }

    std::cout << "Long task completed successfully\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Fast task that also supports cancellation
 */
struct fast_task {
  static constexpr std::string_view name = "fast_task";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "Fast task started (node=" << ctx.node_id() << ")\n";

    // Check cancellation status immediately
    if (ctx.is_cancelled()) {
      std::cout << "Fast task: cancellation detected immediately\n";
      return taskflow::core::task_state::cancelled;
    }

    // Simulate fast work
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "Fast task completed\n";

    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task<long_task>("long_task");
  orch.register_task<fast_task>("fast_task");

  // Build workflow: one long task with two fast tasks in parallel
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "long_task"});  // Long-running task
  bp.add_node(taskflow::workflow::node_def{2, "fast_task"});  // Fast task 1
  bp.add_node(taskflow::workflow::node_def{3, "fast_task"});  // Fast task 2
  bp.add_edge(taskflow::workflow::edge_def{1, 2});            // 1 -> 2
  bp.add_edge(taskflow::workflow::edge_def{1, 3});            // 1 -> 3

  if (!bp.is_valid()) {
    std::cerr << "Invalid blueprint: " << bp.validate() << std::endl;
    return 1;
  }

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Async execution with cancellation ===\n";
  std::cout << "Workflow structure:\n";
  std::cout << "  Node 1: Long task (20 steps, 100ms each)\n";
  std::cout << "  Node 2: Fast task (50ms)\n";
  std::cout << "  Node 3: Fast task (50ms)\n";
  std::cout << "  Dependencies: 1 -> 2, 1 -> 3\n\n";

  // Execute workflow asynchronously
  std::cout << "Starting async workflow execution...\n";
  auto [exec_id, future] = orch.run_async_from_blueprint(1);

  // Wait a bit before cancelling
  std::this_thread::sleep_for(std::chrono::milliseconds(250));
  std::cout << "\n>>> Requesting cancellation after 250ms <<<\n\n";

  // Cancel the execution
  orch.cancel_execution(exec_id);
  std::cout << "Cancellation request sent for execution ID: " << exec_id << "\n\n";

  // Wait for execution to complete
  auto final_state = future.get();
  std::cout << "\n=== Execution Result ===\n";
  std::cout << "Final state: " << taskflow::core::to_string(final_state) << "\n";
  std::cout << "Execution ID: " << exec_id << "\n";

  // Get detailed execution information
  if (const auto* exec = orch.get_execution(exec_id)) {
    std::cout << "\nNode states:\n";
    for (const auto& [node_id, node_state] : exec->node_states()) {
      std::cout << "  Node " << node_id << ": " << taskflow::core::to_string(node_state.state);

      if (!node_state.error_message.empty()) {
        std::cout << " (Error: " << node_state.error_message << ")";
      }

      std::cout << "\n";
    }
  }

  return 0;
}
