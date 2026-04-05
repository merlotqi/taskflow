/**
 * Example 21: Event-driven — IoT Monitoring and Response
 *
 * Demonstrates:
 *  - Device telemetry reporting -> anomaly detection -> response (critical or logging only)
 *  - Conditional edges would cause pending issues on unexecuted branches, so two
 *    separate linear blueprints are used to demonstrate "high temperature emergency"
 *    and "normal temperature logging only" paths, both ending in success
 *  - Async blueprint simulates push notification to operations channel
 */

#include <chrono>
#include <iostream>
#include <taskflow/taskflow.hpp>
#include <thread>

// --- Telemetry task: high temperature scenario ---
struct telemetry_warm_task {
  static constexpr std::string_view name = "telemetry_warm";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("device_id", std::string("DEV-42"));
    ctx.set("temp_c", static_cast<int64_t>(72));
    std::cout << "[Telemetry] High temperature report temp_c=72\n";
    return taskflow::core::task_state::success;
  }
};

// --- Telemetry task: normal temperature scenario ---
struct telemetry_cool_task {
  static constexpr std::string_view name = "telemetry_cool";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("device_id", std::string("DEV-42"));
    ctx.set("temp_c", static_cast<int64_t>(22));
    std::cout << "[Telemetry] Normal temperature report temp_c=22\n";
    return taskflow::core::task_state::success;
  }
};

// --- Detection task: identifies anomalies based on temperature threshold ---
struct detect_task {
  static constexpr std::string_view name = "detect";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto t = ctx.get<int64_t>("temp_c").value_or(0);
    const int64_t critical = (t > 65) ? 1 : 0;
    ctx.set("anomaly_critical", critical);
    std::cout << "[Detection] anomaly_critical=" << critical << "\n";
    return taskflow::core::task_state::success;
  }
};

// --- Response task: critical action for high temperature ---
struct critical_response_task {
  static constexpr std::string_view name = "critical_response";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::cout << "[Response] Emergency: cutting heater relay (simulated)\n";
    return taskflow::core::task_state::success;
  }
};

// --- Response task: logging only for normal temperature ---
struct log_only_task {
  static constexpr std::string_view name = "log_only";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::cout << "[Response] Metrics logged only, no automatic action\n";
    return taskflow::core::task_state::success;
  }
};

// --- Notification task: async push to operations channel ---
struct push_notify_task {
  static constexpr std::string_view name = "push_notify";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::this_thread::sleep_for(std::chrono::milliseconds(50));
    std::cout << "[Push] Operations notification sent (async blueprint)\n";
    return taskflow::core::task_state::success;
  }
};

// --- Blueprint A: critical path for high temperature emergency ---
static taskflow::workflow::workflow_blueprint blueprint_critical_path() {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "telemetry_warm"});
  bp.add_node(taskflow::workflow::node_def{2, "detect"});
  bp.add_node(taskflow::workflow::node_def{3, "critical_response"});

  // Build linear DAG: telemetry_warm -> detect -> critical_response
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 3});
  return bp;
}

// --- Blueprint B: logging path for normal temperature ---
static taskflow::workflow::workflow_blueprint blueprint_log_only_path() {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "telemetry_cool"});
  bp.add_node(taskflow::workflow::node_def{2, "detect"});
  bp.add_node(taskflow::workflow::node_def{4, "log_only"});

  // Build linear DAG: telemetry_cool -> detect -> log_only
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 4});
  return bp;
}

int main() {
  taskflow::engine::orchestrator orch;

  // Register all IoT tasks
  orch.register_task<telemetry_warm_task>("telemetry_warm");
  orch.register_task<telemetry_cool_task>("telemetry_cool");
  orch.register_task<detect_task>("detect");
  orch.register_task<critical_response_task>("critical_response");
  orch.register_task<log_only_task>("log_only");
  orch.register_task<push_notify_task>("push_notify");

  std::cout << "=== Example 21: IoT — High Temperature → Emergency Response Chain ===\n";
  orch.register_blueprint(1, blueprint_critical_path());
  auto [eid_a, st_a] = orch.run_sync_from_blueprint(1);
  std::cout << "Status: " << taskflow::core::to_string(st_a) << " exec_id=" << eid_a << "\n\n";

  std::cout << "=== Example 21: IoT — Normal Temperature → Detection then Logging Only ===\n";
  orch.register_blueprint(2, blueprint_log_only_path());
  auto [eid_b, st_b] = orch.run_sync_from_blueprint(2);
  std::cout << "Status: " << taskflow::core::to_string(st_b) << " exec_id=" << eid_b << "\n\n";

  // Async notification blueprint
  taskflow::workflow::workflow_blueprint notify;
  notify.add_node(taskflow::workflow::node_def{1, "push_notify"});
  orch.register_blueprint(99, std::move(notify));
  auto [eid_n, fut] = orch.run_async_from_blueprint(99);
  (void)eid_n;

  std::cout << "=== Async Operations Notification ===\n";
  fut.wait();
  std::cout << "Async result: " << taskflow::core::to_string(fut.get()) << "\n";

  return (st_a == taskflow::core::task_state::success && st_b == taskflow::core::task_state::success) ? 0 : 1;
}
