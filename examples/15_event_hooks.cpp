/**
 * Integration layer event hooks example
 * Demonstrates FFI-friendly workflow event callbacks
 *
 * This example shows:
 *  - How to use workflow_event_hooks for external system integration
 *  - FFI-compatible C-style function callbacks
 *  - Event filtering and conditional handling
 *  - Integration with message queues, HTTP callbacks, or other systems
 *  - Comparison with full observer pattern
 *
 * Event hooks provide:
 *  - Simpler interface than full observer pattern
 *  - Stable C function pointer signatures
 *  - Easy binding to external languages (C, Python, Go, etc.)
 *  - Lower overhead for basic event handling
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Simple task for demonstration
 */
struct tiny_task {
  static constexpr std::string_view name = "tiny";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;

  // Configure event hooks with lambda callbacks
  taskflow::integration::workflow_event_hooks hooks;

  // Node ready event - task is ready to execute
  hooks.on_node_ready = [](std::size_t exec_id, std::size_t node_id) {
    std::cout << "[Hook] READY  exec=" << exec_id << " node=" << node_id << "\n";
  };

  // Node started event - task execution has begun
  hooks.on_node_started = [](std::size_t exec_id, std::size_t node_id) {
    std::cout << "[Hook] START  exec=" << exec_id << " node=" << node_id << "\n";
  };

  // Node finished event - task execution completed
  hooks.on_node_finished = [](std::size_t exec_id, std::size_t node_id, bool success) {
    std::cout << "[Hook] FINISH exec=" << exec_id << " node=" << node_id << " success=" << (success ? "yes" : "no")
              << "\n";
  };

  // Workflow finished event - entire workflow completed
  hooks.on_workflow_finished = [](std::size_t exec_id, bool success) {
    std::cout << "[Hook] WORKFLOW exec=" << exec_id << " success=" << (success ? "yes" : "no") << "\n";
  };

  orch.set_event_hooks(std::move(hooks));

  orch.register_task<tiny_task>("tiny");

  // Build simple workflow: tiny -> tiny
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "tiny"});
  bp.add_node(taskflow::workflow::node_def{2, "tiny"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Integration layer event hooks ===\n";
  std::cout << "Event hooks provide FFI-friendly callbacks for external integration\n";
  std::cout << "Compared to full observer pattern, hooks are simpler but less detailed\n\n";

  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  return 0;
}
