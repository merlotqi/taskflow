/**
 * Async execution example
 * Demonstrates asynchronous workflow execution with std::future
 *
 * This example shows:
 *  - How to execute workflows asynchronously with run_async()
 *  - How to use std::future to wait for completion
 *  - Concurrent execution of multiple workflows
 *  - Future composition and timeout handling
 *  - Progress monitoring during async execution
 *
 * Async execution is ideal for:
 *  - Non-blocking workflow submission
 *  - Background processing
 *  - Concurrent workflow execution
 *  - Integration with async/await patterns
 */

#include <chrono>
#include <future>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>
#include <vector>

/**
 * Work task that simulates variable duration work
 */
struct work_task {
  int work_time_ms = 30;
  static constexpr std::string_view name = "work";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::this_thread::sleep_for(std::chrono::milliseconds(work_time_ms));
    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task("work", []() { return taskflow::core::task_wrapper{work_task{40}}; });

  // Build workflow: work -> work
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "work"});
  bp.add_node(taskflow::workflow::node_def{2, "work"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Async execution with std::future ===\n";
  std::cout << "Workflow structure:\n";
  std::cout << "  Node 1: work (40ms)\n";
  std::cout << "  Node 2: work (40ms)\n";
  std::cout << "  Dependencies: 1 -> 2\n";
  std::cout << "  Expected total: ~80ms\n\n";

  // Execute workflow asynchronously
  std::cout << "Submitting workflow asynchronously...\n";
  auto [exec_id, future] = orch.run_async_from_blueprint(1);
  std::cout << "Workflow submitted with execution ID: " << exec_id << "\n";
  std::cout << "Main thread can continue doing other work...\n\n";

  // Simulate main thread doing other work
  std::cout << "Main thread: Starting background work...\n";
  std::this_thread::sleep_for(std::chrono::milliseconds(20));
  std::cout << "Main thread: Still working...\n";
  std::cout << "Main thread: Almost done...\n";
  std::cout << "Main thread: Waiting for workflow completion...\n\n";

  // Wait for workflow completion
  auto start_wait = std::chrono::high_resolution_clock::now();
  auto result = future.get();  // Blocks until workflow completes
  auto end_wait = std::chrono::high_resolution_clock::now();

  auto wait_duration = std::chrono::duration_cast<std::chrono::milliseconds>(end_wait - start_wait);

  std::cout << "\n=== Workflow Result ===\n";
  std::cout << "Final state: " << taskflow::core::to_string(result) << "\n";
  std::cout << "Execution ID: " << exec_id << "\n";
  std::cout << "Wait time: " << wait_duration.count() << "ms\n";
  std::cout << "Expected: ~80ms (actual may vary due to scheduling)\n\n";

  // Demonstrate concurrent execution of multiple workflows
  std::cout << "=== Concurrent async execution ===\n";
  std::vector<std::future<taskflow::core::task_state>> futures;
  std::vector<std::size_t> exec_ids;

  for (int i = 0; i < 3; ++i) {
    auto [id, fut] = orch.run_async_from_blueprint(1);
    exec_ids.push_back(id);
    futures.push_back(std::move(fut));
    std::cout << "Submitted workflow " << i + 1 << " with exec_id=" << id << "\n";
  }

  std::cout << "\nWaiting for all workflows to complete...\n";
  for (std::size_t i = 0; i < futures.size(); ++i) {
    auto result = futures[i].get();
    std::cout << "Workflow " << (i + 1) << " (exec_id=" << exec_ids[i]
              << ") completed with state: " << taskflow::core::to_string(result) << "\n";
  }

  return 0;
}
