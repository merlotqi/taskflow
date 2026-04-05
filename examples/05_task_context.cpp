/**
 * Task Context (task_ctx) example
 * Demonstrates shared data bus across workflow execution
 *
 * This example shows:
 *  - How to write values to task_ctx using ctx.set()
 *  - How to read values from task_ctx using ctx.get()
 *  - Type safety and optional return values
 *  - Data visibility and isolation guarantees
 *  - Recommended usage patterns
 *
 * task_ctx is a per-execution shared key-value store.
 * All tasks within the same execution see the same context.
 * Suitable for lightweight data: order IDs, feature flags,
 * and intermediate calculation results.
 */

#include <iostream>
#include <optional>
#include <string>
#include <taskflow/taskflow.hpp>

/**
 * Producer task that writes values into shared context
 */
struct producer_task {
  static constexpr std::string_view name = "producer";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    // Store different types in context
    ctx.set("order_id", std::string("ORD-10086"));
    ctx.set("shard_id", static_cast<int64_t>(7));
    ctx.set("retry_count", static_cast<int64_t>(3));

    std::cout << "Producer: stored order_id, shard_id and retry_count\n";
    return taskflow::core::task_state::success;
  }
};

/**
 * Consumer task that reads values from shared context
 */
struct consumer_task {
  static constexpr std::string_view name = "consumer";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    // get() returns std::optional<T>
    auto order_id = ctx.get<std::string>("order_id");
    auto shard_id = ctx.get<int64_t>("shard_id");
    auto retry_count = ctx.get<int64_t>("retry_count");
    auto missing_key = ctx.get<int64_t>("non_existent_key");

    std::cout << "Consumer: reading from context:\n";
    std::cout << "  order_id     = " << (order_id ? *order_id : "(not found)") << "\n";
    std::cout << "  shard_id     = " << (shard_id ? std::to_string(*shard_id) : "(not found)") << "\n";
    std::cout << "  retry_count  = " << (retry_count ? std::to_string(*retry_count) : "(not found)") << "\n";
    std::cout << "  missing_key  = " << (missing_key ? std::to_string(*missing_key) : "(not found)") << "\n";

    return taskflow::core::task_state::success;
  }
};

int main() {
  taskflow::engine::orchestrator orch;
  orch.register_task<producer_task>("producer");
  orch.register_task<consumer_task>("consumer");

  // Build workflow: producer -> consumer
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "producer"});
  bp.add_node(taskflow::workflow::node_def{2, "consumer"});
  bp.add_edge(taskflow::workflow::edge_def{1, 2});

  if (!bp.is_valid()) {
    std::cerr << "Invalid blueprint: " << bp.validate() << std::endl;
    return 1;
  }

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== task_ctx data passing 1 -> 2 ===\n";
  auto [exec_id, result] = orch.run_sync_from_blueprint(1);

  std::cout << "\nExecution result: " << taskflow::core::to_string(result) << " | Execution ID: " << exec_id << "\n";

  // Context remains accessible after execution completes
  if (const auto* exec = orch.get_execution(exec_id)) {
    auto persisted_order_id = exec->context().get<std::string>("order_id");
    if (persisted_order_id) {
      std::cout << "\nContext preserved after execution: order_id = " << *persisted_order_id << "\n";
    }
  }

  return 0;
}
