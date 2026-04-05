/**
 * Parallel tasks execution example
 * Demonstrates fan-out/fan-in pattern with thread pool execution
 *
 * This example shows:
 *  - Configuring custom thread pool size
 *  - Fan-out pattern: root node triggers multiple parallel tasks
 *  - Fan-in pattern: join node waits for all parallel tasks
 *  - Performance measurement and speedup calculation
 *  - Thread affinity and execution observation
 */

#include <atomic>
#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

/**
 * Worker task that simulates CPU bound work
 * Demonstrates parallel execution across multiple threads
 */
struct worker_task {
  int work_time_ms;

  static constexpr std::string_view name = "worker";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    static std::atomic<int> active_threads{0};

    int current = ++active_threads;
    std::cout << "[Thread " << std::this_thread::get_id() << "] Starting node " << ctx.node_id()
              << " (active: " << current << ")" << std::endl;

    // Simulate actual work
    std::this_thread::sleep_for(std::chrono::milliseconds(work_time_ms));

    current = --active_threads;
    std::cout << "[Thread " << std::this_thread::get_id() << "] Completed node " << ctx.node_id()
              << " (active: " << current << ")" << std::endl;

    return taskflow::core::task_state::success;
  }
};

int main() {
  // Configure thread pool with 4 worker threads
  // Adjust this number to see how parallelism affects total runtime
  const std::size_t thread_count = 4;
  auto tp = std::make_unique<taskflow::engine::default_thread_pool>(thread_count);
  taskflow::engine::orchestrator orch(std::move(tp));

  orch.register_task<worker_task>("worker");

  // Build DAG with classic fan-out/fan-in structure:
  //
  //      1 (start)
  //    / | \'
  //   2  3  4  (parallel execution)
  //    \ | /
  //      5 (join)
  //
  taskflow::workflow::workflow_blueprint bp;

  bp.add_node(taskflow::workflow::node_def{1, "worker"});
  bp.add_node(taskflow::workflow::node_def{2, "worker"});
  bp.add_node(taskflow::workflow::node_def{3, "worker"});
  bp.add_node(taskflow::workflow::node_def{4, "worker"});
  bp.add_node(taskflow::workflow::node_def{5, "worker"});

  // All parallel nodes depend on root node
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{1, 3});
  bp.add_edge(taskflow::workflow::edge_def{1, 4});

  // Join node depends on all parallel nodes
  bp.add_edge(taskflow::workflow::edge_def{2, 5});
  bp.add_edge(taskflow::workflow::edge_def{3, 5});
  bp.add_edge(taskflow::workflow::edge_def{4, 5});

  std::cout << "=== Parallel tasks execution ===" << std::endl;
  std::cout << "Thread pool size: " << thread_count << std::endl;
  std::cout << "Each task takes 300ms" << std::endl;
  std::cout << "Sequential execution would take: 1500ms" << std::endl;

  orch.register_blueprint(3, std::move(bp));

  auto start = std::chrono::high_resolution_clock::now();
  auto [exec_id, result] = orch.run_sync_from_blueprint(3);
  auto end = std::chrono::high_resolution_clock::now();

  auto duration = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  std::cout << "\nActual total execution time: " << duration.count() << "ms" << std::endl;
  std::cout << "Execution result: " << taskflow::core::to_string(result) << std::endl;
  std::cout << "Theoretical speedup: ~2.5x" << std::endl;

  return 0;
}
