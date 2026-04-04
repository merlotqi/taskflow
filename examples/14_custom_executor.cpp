/**
 * Custom executor example
 * Demonstrates how to implement and inject custom parallel executors
 *
 * This example shows:
 *  - How to create custom parallel_executor implementations
 *  - Executor injection through orchestrator constructor
 *  - Different execution strategies (inline, thread pool, coroutine)
 *  - Parallelism reporting and resource management
 *  - Debugging with inline executor
 *
 * Custom executors allow:
 *  - Integration with existing thread pools
 *  - Coroutine-based execution
 *  - Priority-based scheduling
 *  - Resource-constrained environments
 */

#include <functional>
#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Simple step task for demonstration
 */
struct step_task {
  static constexpr std::string_view name = "step";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "Step running on node " << ctx.node_id() << " (scheduled by custom executor)\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Inline executor that runs tasks immediately on submit thread
 * Useful for debugging and single-threaded environments
 */
struct inline_executor final : taskflow::engine::parallel_executor {
  void submit(std::function<void()> task) override {
    if (task) task();  // Execute immediately on caller thread
  }

  void wait_all() override {}  // No-op for inline execution

  [[nodiscard]] std::size_t parallelism() const override {
    return 1;  // Report single-threaded execution
  }
};

/**
 * Thread pool executor using std::thread
 * Demonstrates custom thread pool implementation
 */
struct thread_pool_executor final : taskflow::engine::parallel_executor {
  std::vector<std::thread> threads_;
  std::mutex queue_mutex_;
  std::condition_variable cv_;
  std::deque<std::function<void()>> task_queue_;
  bool stop_ = false;

  explicit thread_pool_executor(std::size_t thread_count) {
    for (std::size_t i = 0; i < thread_count; ++i) {
      threads_.emplace_back([this] {
        while (true) {
          std::function<void()> task;

          {
            std::unique_lock<std::mutex> lock(queue_mutex_);
            cv_.wait(lock, [this] { return stop_ || !task_queue_.empty(); });

            if (stop_ && task_queue_.empty()) return;

            task = std::move(task_queue_.front());
            task_queue_.pop_front();
          }

          if (task) task();
        }
      });
    }
  }

  void submit(std::function<void()> task) override {
    {
      std::scoped_lock lock(queue_mutex_);
      task_queue_.emplace_back(std::move(task));
    }
    cv_.notify_one();
  }

  void wait_all() override {
    {
      std::scoped_lock lock(queue_mutex_);
      stop_ = true;
    }
    cv_.notify_all();
    for (auto& thread : threads_) {
      thread.join();
    }
  }

  [[nodiscard]] std::size_t parallelism() const override { return threads_.size(); }
};

int main() {
  std::cout << "=== Custom Executor Examples ===\n\n";

  // Example 1: Inline executor (synchronous execution)
  {
    auto inline_exec = std::make_unique<inline_executor>();
    taskflow::engine::orchestrator orch(std::move(inline_exec));

    orch.register_task<step_task>("step");

    taskflow::workflow::workflow_blueprint bp;
    bp.add_node(taskflow::workflow::node_def{1, "step"});
    bp.add_node(taskflow::workflow::node_def{2, "step"});
    bp.add_edge(taskflow::workflow::edge_def{1, 2});

    orch.register_blueprint(1, std::move(bp));

    std::cout << "1. Running with inline executor (synchronous):\n";
    auto [exec_id, result] = orch.run_sync_from_blueprint(1);
    std::cout << "   Result: " << taskflow::core::to_string(result) << " | Exec ID: " << exec_id << "\n\n";
  }

  // Example 2: Thread pool executor (parallel execution)
  {
    auto thread_exec = std::make_unique<thread_pool_executor>(4);
    taskflow::engine::orchestrator orch(std::move(thread_exec));

    orch.register_task<step_task>("step");

    taskflow::workflow::workflow_blueprint bp;
    bp.add_node(taskflow::workflow::node_def{1, "step"});
    bp.add_node(taskflow::workflow::node_def{2, "step"});
    bp.add_edge(taskflow::workflow::edge_def{1, 2});

    orch.register_blueprint(2, std::move(bp));

    std::cout << "2. Running with thread pool executor (4 threads):\n";
    auto [exec_id, result] = orch.run_sync_from_blueprint(2);
    std::cout << "   Result: " << taskflow::core::to_string(result) << " | Exec ID: " << exec_id << "\n\n";
  }

  return 0;
}
