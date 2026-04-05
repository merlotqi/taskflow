/**
 * Conditional edges example
 * Demonstrates dynamic branching based on runtime context values
 *
 * This example shows:
 *  - How to attach condition functions to edges
 *  - Runtime branching decisions based on task_ctx data
 *  - Mutual exclusive branch behavior
 *  - Pending state semantics for unselected branches
 *  - Best practices for conditional workflows
 *
 * IMPORTANT: When using mutually exclusive conditional edges,
 * unselected branches will remain in pending state. For production
 * use, consider separate blueprints or proper join patterns.
 */

#include <iostream>
#include <taskflow/taskflow.hpp>

/**
 * Task that sets branch selection flag in context
 */
struct set_branch_task {
  bool is_vip = false;
  static constexpr std::string_view name = "set_branch";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("vip_flag", static_cast<int64_t>(is_vip ? 1 : 0));
    std::cout << "Node " << ctx.node_id() << " set vip=" << std::boolalpha << is_vip << "\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Leaf task executed on selected branch
 */
struct leaf_task {
  std::string branch_name;
  static constexpr std::string_view name = "leaf";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::cout << "Executing branch: " << branch_name << " (node=" << ctx.node_id() << ")\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Run conditional workflow scenario with given configuration
 */
static void run_scenario(const char* title, bool vip_mode) {
  std::cout << "=== " << title << " ===" << std::endl;

  taskflow::engine::orchestrator orch;

  // Register tasks with captured parameters
  orch.register_task("set_branch", [vip_mode]() { return taskflow::core::task_wrapper{set_branch_task{vip_mode}}; });
  orch.register_task("vip_branch", []() { return taskflow::core::task_wrapper{leaf_task{"VIP Processing Path"}}; });
  orch.register_task("normal_branch",
                     []() { return taskflow::core::task_wrapper{leaf_task{"Standard Processing Path"}}; });

  // Build workflow with conditional edges
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "set_branch"});
  bp.add_node(taskflow::workflow::node_def{2, "vip_branch"});
  bp.add_node(taskflow::workflow::node_def{3, "normal_branch"});

  // Edge condition: only follow this edge if vip_flag is true
  bp.add_edge(taskflow::workflow::edge_def{
      1, 2, [](const taskflow::core::task_ctx& ctx) { return ctx.get<int64_t>("vip_flag").value_or(0) != 0; }});

  // Edge condition: only follow this edge if vip_flag is false
  bp.add_edge(taskflow::workflow::edge_def{
      1, 3, [](const taskflow::core::task_ctx& ctx) { return ctx.get<int64_t>("vip_flag").value_or(0) == 0; }});

  orch.register_blueprint(1, std::move(bp));
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "Execution ID: " << exec_id << " | Overall state: " << taskflow::core::to_string(result) << "\n";

  // Inspect individual node states after execution
  if (const auto* exec = orch.get_execution(exec_id)) {
    for (const auto& [node_id, node_state] : exec->node_states()) {
      std::cout << "  Node " << node_id << ": " << taskflow::core::to_string(node_state.state) << "\n";
    }
  }

  std::cout << std::endl;
}

int main() {
  run_scenario("VIP Scenario - only VIP branch executes", true);
  run_scenario("Standard Scenario - only normal branch executes", false);

  std::cout << "NOTE: Unselected branches remain in PENDING state\n";
  std::cout << "This is expected behavior for conditional edges\n";

  return 0;
}
