/**
 * Example 20: Multi-step Approval Workflow
 *
 * Demonstrates:
 *  - Submit task writes amount to context
 *  - Department approval simulates waiting with timeout escalation (using steady_clock
 *    since engine has no built-in timeout)
 *  - Finance approval branches based on amount: low amount auto-approves, high amount
 *    triggers manual review simulation
 *  - audit_log records node state transitions for post-execution history review
 *  - Single-node logic used for dynamic levels to avoid OR-join conflicts that occur
 *    when conditional edges skip finance node and merge into archive
 */

#include <chrono>
#include <iostream>
#include <memory>
#include <taskflow/core/audit_log.hpp>
#include <taskflow/taskflow.hpp>
#include <thread>

// --- Submit task: creates approval request ---
struct submit_task {
  static constexpr std::string_view name = "submit";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("applicant", std::string("zhang"));
    ctx.set("amount_cents", static_cast<int64_t>(250000));  // 2500.00 CNY — change to 5000 for high amount demo
    ctx.set("deadline_ms", static_cast<int64_t>(80));
    std::cout << "[Submit] Application created\n";
    return taskflow::core::task_state::success;
  }
};

// --- Department approval task: simulates review with timeout escalation ---
struct dept_approval_task {
  static constexpr std::string_view name = "dept_approval";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    const auto deadline_ms = ctx.get<int64_t>("deadline_ms").value_or(100);
    const auto start = std::chrono::steady_clock::now();
    std::cout << "[Department] Review in progress...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(120));  // Exceeds deadline to simulate timeout

    const auto elapsed =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start);
    if (elapsed.count() > deadline_ms) {
      ctx.set("escalated", static_cast<int64_t>(1));
      std::cout << "[Department] Timeout, escalation flag set escalated=1\n";
    } else {
      ctx.set("escalated", static_cast<int64_t>(0));
    }
    ctx.set("dept_ok", static_cast<int64_t>(1));
    return taskflow::core::task_state::success;
  }
};

// --- Finance approval task: amount-based branching logic ---
struct finance_approval_task {
  static constexpr std::string_view name = "finance_approval";
  static constexpr int64_t threshold_cents = 100000;  // > 1000 CNY requires explicit finance review

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    const auto amount = ctx.get<int64_t>("amount_cents").value_or(0);
    if (amount <= threshold_cents) {
      std::cout << "[Finance] Amount below threshold, auto-approved\n";
      ctx.set("finance_ok", static_cast<int64_t>(1));
      return taskflow::core::task_state::success;
    }
    std::cout << "[Finance] High amount review in progress...\n";
    std::this_thread::sleep_for(std::chrono::milliseconds(40));
    ctx.set("finance_ok", static_cast<int64_t>(1));
    std::cout << "[Finance] Review approved\n";
    return taskflow::core::task_state::success;
  }
};

// --- Archive task: finalizes and logs approval result ---
struct archive_task {
  static constexpr std::string_view name = "archive";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto esc = ctx.get<int64_t>("escalated").value_or(0);
    std::cout << "[Archive] Case closed escalated=" << esc << "\n";
    return taskflow::core::task_state::success;
  }
};

int main() {
  auto audit = std::make_shared<taskflow::core::audit_log>();
  taskflow::engine::orchestrator orch;
  orch.set_audit_log(audit);

  // Register all approval tasks
  orch.register_task<submit_task>("submit");
  orch.register_task<dept_approval_task>("dept_approval");
  orch.register_task<finance_approval_task>("finance_approval");
  orch.register_task<archive_task>("archive");

  // Build linear DAG: submit -> dept_approval -> finance_approval -> archive
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "submit"});
  bp.add_node(taskflow::workflow::node_def{2, "dept_approval"});
  bp.add_node(taskflow::workflow::node_def{3, "finance_approval"});
  bp.add_node(taskflow::workflow::node_def{4, "archive"});

  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 3});
  bp.add_edge(taskflow::workflow::edge_def{3, 4});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Example 20: Approval Workflow ===\n";
  auto [eid, st] = orch.run_sync_from_blueprint(1);
  std::cout << "Completed: " << taskflow::core::to_string(st) << " exec_id=" << eid << "\n";

  // Print audit trail
  std::cout << "\n--- Audit Trail (exec " << eid << ") ---\n";
  for (const auto& e : audit->get_history(eid)) {
    std::cout << " node=" << e.node_id << " " << taskflow::core::to_string(e.old_state) << " -> "
              << taskflow::core::to_string(e.new_state);
    if (!e.error_message.empty()) std::cout << " err=" << e.error_message;
    std::cout << "\n";
  }

  return st == taskflow::core::task_state::success ? 0 : 1;
}
