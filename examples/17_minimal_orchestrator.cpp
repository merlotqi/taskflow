/**
 * Minimal orchestrator example
 * Demonstrates basic workflow orchestration with task results
 *
 * This example shows:
 *  - How to create and register tasks with the orchestrator
 *  - Building a simple DAG workflow with dependencies
 *  - Using task results and data passing between tasks
 *  - Observer pattern for logging workflow events
 *  - Retrieving execution results and context data
 *
 * Key concepts:
 *  - Tasks can return results via ctx.set() and ctx.set_result()
 *  - Data is passed through the task context (ctx)
 *  - Observers can monitor workflow events (start, complete, fail)
 *  - Execution results are available after workflow completion
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
struct task_result {
  std::string message;
  int code = 0;
};

// Helper functions for task result serialization
std::any to_task_value(const task_result& result) {
  std::unordered_map<std::string, std::any> map;
  map["message"] = result.message;
  map["code"] = static_cast<int64_t>(result.code);
  return map;
}

bool from_task_value(const std::any& any_val, task_result& result) {
  try {
    const auto& map = std::any_cast<const std::unordered_map<std::string, std::any>&>(any_val);
    result.message = std::any_cast<std::string>(map.at("message"));
    result.code = static_cast<int>(std::any_cast<int64_t>(map.at("code")));
    return true;
  } catch (...) {
    return false;
  }
}
// --- A simple task that returns a result on the data bus ---
struct print_task {
  static constexpr std::string_view name = "print";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto nid = ctx.node_id();
    std::string display = "node_" + std::to_string(nid);

    if (auto label = ctx.get<std::string>("label")) {
      display = *label;
    }

    std::cout << "[Task] " << display << " (node=" << nid << ")\n";

    task_result result{display + "_done", 0};
    ctx.set(display + "_result", result);

    ctx.set("last_node", static_cast<int64_t>(nid));

    return taskflow::core::task_state::success;
  }
};
// --- Observer: logs events to stdout ---
class console_observer : public taskflow::obs::observer {
 public:
  void on_task_start(std::size_t, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override {
    std::cout << "  START node=" << node_id << " type=" << task_type << " attempt=" << attempt << "\n";
  }
  void on_task_complete(std::size_t, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds dur) noexcept override {
    std::cout << "  DONE  node=" << node_id << " type=" << task_type << " took=" << dur.count() << "ms\n";
  }
  void on_task_fail(std::size_t, std::size_t node_id, std::string_view task_type, std::string_view error,
                    std::chrono::milliseconds dur) noexcept override {
    std::cout << "  FAIL  node=" << node_id << " type=" << task_type << " err=" << error << " took=" << dur.count()
              << "ms\n";
  }
  void on_workflow_complete(std::size_t, taskflow::core::task_state state,
                            std::chrono::milliseconds dur) noexcept override {
    std::cout << "WORKFLOW " << taskflow::core::to_string(state) << " took=" << dur.count() << "ms\n";
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task<print_task>("print");

  // Build DAG: 1 -> 2, 1 -> 3
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "print"});
  bp.add_node(taskflow::workflow::node_def{2, "print"});
  bp.add_node(taskflow::workflow::node_def{3, "print"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{1, 3});

  std::cout << "Blueprint validation: " << (bp.is_valid() ? "OK" : bp.validate()) << "\n";

  orch.register_blueprint(100, std::move(bp));

  console_observer logger;
  orch.add_observer(&logger);

  std::cout << "\n--- Running workflow ---\n";
  auto [exec_id, result] = orch.run_sync_from_blueprint(100);

  std::cout << "\n--- Result ---\n";
  std::cout << "Execution " << exec_id << ": " << taskflow::core::to_string(result) << "\n";

  const auto* exec = orch.get_execution(exec_id);
  if (exec) {
    std::cout << "Data keys: ";
    for (const auto& [key, _] : exec->context().data()) {
      std::cout << key << " ";
    }
    std::cout << "\n";

    if (auto last_node = exec->context().get<int64_t>("last_node")) {
      std::cout << "Last executed node: " << *last_node << "\n";
    }
  }

  return result == taskflow::core::task_state::success ? 0 : 1;
}
