/**
 * Metrics collection example
 * Demonstrates performance metrics aggregation with metrics_observer
 *
 * This example shows:
 *  - How to use built-in metrics_observer for performance tracking
 *  - Task type-based metrics aggregation
 *  - Start/success/fail counts and duration statistics
 *  - Min/Max/Average duration calculation
 *  - Real-time metrics collection during execution
 *
 * metrics_observer automatically tracks:
 *  - Task start counts
 *  - Success and failure counts
 *  - Total, min, max, and average durations
 *  - Metrics per task type
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

/**
 * Slow task that simulates CPU intensive work
 */
struct slow_task {
  int work_time_ms = 100;
  static constexpr std::string_view name = "slow_op";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::this_thread::sleep_for(std::chrono::milliseconds(work_time_ms));
    return taskflow::core::task_state::success;
  }
};

/**
 * Fast task that completes immediately
 */
struct fast_task {
  static constexpr std::string_view name = "fast_op";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;

  // Create and register metrics observer
  taskflow::obs::metrics_observer metrics;
  orch.add_observer(&metrics);

  // Register tasks with different configurations
  orch.register_task("slow_op", []() { return taskflow::core::task_wrapper{slow_task{80}}; });
  orch.register_task<fast_task>("fast_op");

  // Build workflow with mixed task types
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "slow_op"});
  bp.add_node(taskflow::workflow::node_def{2, "fast_op"});
  bp.add_node(taskflow::workflow::node_def{3, "slow_op"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 3});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== metrics_observer performance tracking ===\n";
  std::cout << "Workflow structure:\n";
  std::cout << "  Node 1: slow_op (80ms)\n";
  std::cout << "  Node 2: fast_op (immediate)\n";
  std::cout << "  Node 3: slow_op (80ms)\n";
  std::cout << "  Dependencies: 1 -> 2 -> 3\n\n";

  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nWorkflow execution: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id
            << "\n\n";

  // Display aggregated metrics
  std::cout << "=== Performance Metrics ===\n";
  for (const auto& [task_type, metrics_data] : metrics.all_metrics()) {
    std::cout << "Task Type: " << task_type << "\n";
    std::cout << "  Starts:    " << metrics_data.start_count << "\n";
    std::cout << "  Successes: " << metrics_data.success_count << "\n";
    std::cout << "  Failures:  " << metrics_data.fail_count << "\n";
    std::cout << "  Total:     " << metrics_data.total_duration_ms.count() << "ms\n";
    std::cout << "  Min:       " << metrics_data.min_duration_ms.count() << "ms\n";
    std::cout << "  Max:       " << metrics_data.max_duration_ms.count() << "ms\n";
    std::cout << "  Average:   "
              << (metrics_data.start_count > 0 ? metrics_data.total_duration_ms.count() / metrics_data.start_count : 0)
              << "ms\n";
    std::cout << "\n";
  }

  // Clean up observer
  orch.remove_observer(&metrics);

  return 0;
}
