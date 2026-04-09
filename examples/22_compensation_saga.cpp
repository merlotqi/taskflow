/**
 * Example 22: Compensation (Saga-style)
 *
 * Demonstrates optional per-node compensate_task_type and orchestrator_run_options:
 *  - compensate_on_failure: after a node fails, run compensate tasks in reverse order of forward successes (LIFO)
 *  - compensate_on_cancel: same for nodes that reached success when the workflow was cancelled
 */

#include <iostream>
#include <string>
#include <taskflow/taskflow.hpp>

struct reserve_task {
  static constexpr std::string_view name = "reserve";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("reserved", std::string("item-42"));
    std::cout << "[forward] reserved resource\n";
    return taskflow::core::task_state::success;
  }
};

struct charge_task {
  static constexpr std::string_view name = "charge";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::cout << "[forward] charge failed\n";
    return taskflow::core::task_state::failed;
  }
};

struct release_task {
  static constexpr std::string_view name = "release";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto r = ctx.get<std::string>("reserved").value_or("?");
    std::cout << "[compensate] released reservation key=" << r << "\n";
    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task<reserve_task>("reserve");
  orch.register_task<charge_task>("charge");
  orch.register_task<release_task>("release");

  taskflow::workflow::workflow_blueprint bp;
  taskflow::workflow::node_def n1{1, "reserve"};
  n1.compensate_task_type = std::string{"release"};
  bp.add_node(std::move(n1));
  bp.add_node(taskflow::workflow::node_def{2, "charge"});
  bp.add_edge({1, 2});

  orch.register_blueprint(1, std::move(bp));

  taskflow::engine::orchestrator_run_options opts;
  opts.compensate_on_failure = true;

  auto [exec_id, st] = orch.run_sync_from_blueprint(1, opts);
  std::cout << "workflow state=" << taskflow::core::to_string(st) << " exec=" << exec_id << "\n";

  const auto* ex = orch.get_execution(exec_id);
  if (ex) {
    std::cout << "node1=" << taskflow::core::to_string(ex->get_node_state(1).state)
              << " node2=" << taskflow::core::to_string(ex->get_node_state(2).state) << "\n";
  }

  (void)st;
  return 0;
}
