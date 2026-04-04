/**
 * Cancellation token example
 * Demonstrates workflow cancellation and task interruption
 *
 * This example shows:
 *  - How to cancel running workflows using cancellation tokens
 *  - Task-level cancellation detection and graceful shutdown
 *  - Progress reporting during long-running operations
 *  - Asynchronous workflow execution with cancellation support
 *  - Checking cancellation status in both long and short tasks
 *
 * Key concepts:
 *  - Cancellation tokens are checked periodically by tasks
 *  - Tasks should check ctx.is_cancelled() to detect cancellation
 *  - Cancelled tasks return task_state::cancelled
 *  - Workflow execution can be cancelled at any time
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>
/**
 * Cancellation token example
 * Demonstrates workflow cancellation and task interruption
 *
 * This example shows:
 *  - How to cancel running workflows using cancellation tokens
 *  - Task-level cancellation detection and graceful shutdown
 *  - Progress reporting during long-running operations
 *  - Asynchronous workflow execution with cancellation support
 *  - Checking cancellation status in both long and short tasks
 *
 * Key concepts:
 *  - Cancellation tokens are checked periodically by tasks
 *  - Tasks should check ctx.is_cancelled() to detect cancellation
 *  - Cancelled tasks return task_state::cancelled
 *  - Workflow execution can be cancelled at any time
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

// Simulate a long-running task
struct long_running_task {
  static constexpr std::string_view name = "long_running";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "[LongTask] Starting long-running task (node=" << ctx.node_id() << ")\n";

    // Simulate execution in 10 steps
    for (int i = 0; i < 10; ++i) {
      // Check if cancelled
      if (ctx.is_cancelled()) {
        std::cout << "[LongTask] Task cancelled at step " << i << "\n";
        return taskflow::core::task_state::cancelled;
      }

      // Simulate work for each step
      std::cout << "[LongTask] Executing step " << i << "/10\n";
      std::this_thread::sleep_for(std::chrono::milliseconds(500));

      // Update progress
      ctx.report_progress(static_cast<float>(i + 1) / 10.0f);
    }

    std::cout << "[LongTask] Task completed\n";
    return taskflow::core::task_state::success;
  }
};

// Fast task that checks cancellation status
struct fast_task {
  static constexpr std::string_view name = "fast";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "[FastTask] Fast task started (node=" << ctx.node_id() << ")\n";

    // Fast task also checks cancellation status
    if (ctx.is_cancelled()) {
      std::cout << "[FastTask] Task cancelled\n";
      return taskflow::core::task_state::cancelled;
    }

    // Simulate fast work
    std::this_thread::sleep_for(std::chrono::milliseconds(100));
    std::cout << "[FastTask] Task completed\n";

    return taskflow::core::task_state::success;
  }
};

int main() {
  std::cout << "=== Cancellation Token Example ===\n\n";

  // Create orchestrator
  taskflow::engine::orchestrator orch;
  orch.register_task<long_running_task>("long_running");
  orch.register_task<fast_task>("fast");
  // Build workflow: one long-running task with two fast tasks in parallel
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "long_running"});  // Long-running task
  bp.add_node(taskflow::workflow::node_def{2, "fast"});          // Fast task 1
  bp.add_node(taskflow::workflow::node_def{3, "fast"});          // Fast task 2
  bp.add_edge(taskflow::workflow::edge_def{1, 2});               // 1 -> 2
  bp.add_edge(taskflow::workflow::edge_def{1, 3});               // 1 -> 3

  std::cout << "Blueprint validation: " << (bp.is_valid() ? "OK" : bp.validate()) << "\n\n";

  // Register blueprint
  orch.register_blueprint(200, std::move(bp));

  // Execute workflow asynchronously - returns pair<execution_id, future>
  std::cout << "Starting async workflow execution...\n";
  auto [exec_id, fut] = orch.run_async_from_blueprint(200);

  // Wait for a while
  std::this_thread::sleep_for(std::chrono::seconds(1));

  // Get execution object and cancel
  auto* exec = orch.get_execution(exec_id);
  if (exec) {
    std::cout << "\n>>> After waiting 1 second, calling cancel() to cancel workflow <<<\n\n";
    exec->cancel();

    // Can also check status directly
    std::cout << "Cancellation status: " << (exec->is_cancelled() ? "Cancelled" : "Not cancelled") << "\n\n";
  }

  // Wait for completion
  auto result = fut.get();

  std::cout << "\n=== Execution Result ===\n";
  std::cout << "Final state: " << taskflow::core::to_string(result) << "\n";

  // Get execution details
  exec = orch.get_execution(exec_id);
  if (exec) {
    std::cout << "\nNode states:\n";
    for (const auto& [node_id, ns] : exec->node_states()) {
      std::cout << "  Node " << node_id << ": " << taskflow::core::to_string(ns.state);
      if (!ns.error_message.empty()) {
        std::cout << " (Error: " << ns.error_message << ")";
      }
      std::cout << "\n";
    }
  }

  return 0;
}
