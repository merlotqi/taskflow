/**
 * Observer example
 * Demonstrates event-driven logging and monitoring with observer pattern
 *
 * This example shows:
 *  - How to implement custom observer by inheriting taskflow::obs::observer
 *  - Event callbacks: on_task_start, on_task_complete, on_task_fail, on_workflow_complete
 *  - Structured logging with timestamps and execution context
 *  - Observer lifecycle management (add/remove)
 *  - Integration with tracing and monitoring systems
 *
 * Observers are ideal for:
 *  - Application logging and auditing
 *  - Performance monitoring and metrics
 *  - Distributed tracing integration
 *  - Error tracking and alerting
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

/**
 * Simple ping task that simulates work
 */
struct ping_task {
  static constexpr std::string_view name = "ping";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::this_thread::sleep_for(std::chrono::milliseconds(15));
    return taskflow::core::task_state::success;
  }
};

/**
 * Console logger observer that implements all callback methods
 */
class console_observer final : public taskflow::obs::observer {
 public:
  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override {
    std::cout << "[Observer] START  exec=" << exec_id << " node=" << node_id << " type=" << task_type
              << " attempt=" << attempt << "\n";
  }

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds duration) noexcept override {
    std::cout << "[Observer] SUCCESS exec=" << exec_id << " node=" << node_id << " type=" << task_type
              << " duration=" << duration.count() << "ms\n";
  }

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type, std::string_view error,
                    std::chrono::milliseconds duration) noexcept override {
    std::cout << "[Observer] FAIL    exec=" << exec_id << " node=" << node_id << " type=" << task_type
              << " error=" << error << " duration=" << duration.count() << "ms\n";
  }

  void on_workflow_complete(std::size_t exec_id, taskflow::core::task_state state,
                            std::chrono::milliseconds duration) noexcept override {
    std::cout << "[Observer] WORKFLOW exec=" << exec_id << " state=" << taskflow::core::to_string(state)
              << " duration=" << duration.count() << "ms\n";
  }
};

int main() {
  taskflow::engine::orchestrator orch;

  // Create and register observer
  console_observer observer;
  orch.add_observer(&observer);

  orch.register_task<ping_task>("ping");

  // Build simple workflow: ping -> ping
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ping"});
  bp.add_node(taskflow::workflow::node_def{2, "ping"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Observer logging example ===\n";
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution completed: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  // Clean up observer
  orch.remove_observer(&observer);

  return 0;
}
