/**
 * Example 18: Microservice Orchestration — E-commerce Order Processing
 *
 * Demonstrates:
 *  - Two blueprints (in-stock / procurement) both end with success, avoiding pending
 *    states from unexecuted branches due to conditional edges
 *  - Payment node configured with retry policy, simulating failures using counter
 *    in task_ctx
 *  - Main workflow runs synchronously, separate blueprint runs asynchronously
 *    simulating notification service
 *  - memory_result_storage for result collection; state snapshots via state_storage_factory (sqlite or memory fallback)
 */

#include <chrono>
#include <cstdlib>
#include <iostream>
#include <memory>
#include <string>
#include <taskflow/engine/default_thread_pool.hpp>
#include <taskflow/storage/memory_result_storage.hpp>
#include <taskflow/taskflow.hpp>
#include <thread>

// --- Order validation task: validates order and sets basic info ---
struct order_validation_task {
  static constexpr std::string_view name = "order_validation";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("order_id", std::string("ORD-10086"));
    ctx.set("customer_id", static_cast<int64_t>(12345));
    ctx.set("total_amount_cents", static_cast<int64_t>(19999));
    std::cout << "[Validation] Order is valid\n";
    return taskflow::core::task_state::success;
  }
};

// --- Inventory check: in-stock path ---
struct inventory_in_stock_task {
  static constexpr std::string_view name = "inventory_ok";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("in_stock", static_cast<int64_t>(1));
    ctx.set("product_id", std::string("PROD-5001"));
    std::cout << "[Inventory] Stock sufficient\n";
    return taskflow::core::task_state::success;
  }
};

// --- Inventory check: low-stock / out-of-stock path ---
struct inventory_low_task {
  static constexpr std::string_view name = "inventory_low";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("in_stock", static_cast<int64_t>(0));
    ctx.set("out_of_stock_product", std::string("PROD-5001"));
    std::cout << "[Inventory] Insufficient, triggering procurement branch\n";
    return taskflow::core::task_state::success;
  }
};

// --- Procurement task: reorder out-of-stock products ---
struct procurement_task {
  static constexpr std::string_view name = "procurement";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto p = ctx.get<std::string>("out_of_stock_product").value_or("?");
    std::cout << "[Procurement] Purchase order placed for product=" << p << "\n";
    ctx.set("procurement_status", std::string("initiated"));
    return taskflow::core::task_state::success;
  }
};

// --- Payment task: retry configured, fails first 2 attempts ---
struct payment_task {
  static constexpr std::string_view name = "payment";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto n = ctx.get<int64_t>("_pay_attempts").value_or(0);
    ctx.set("_pay_attempts", n + 1);
    if (n < 2) {
      std::cout << "[Payment] Gateway flaky, failing to trigger retry (attempt " << (n + 1) << ")\n";
      return taskflow::core::task_state::failed;
    }
    ctx.set("payment_id", std::string("PAY-98765"));
    std::cout << "[Payment] Successful\n";
    return taskflow::core::task_state::success;
  }
};

// --- Shipping task: notify logistics ---
struct shipping_task {
  static constexpr std::string_view name = "shipping";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    std::this_thread::sleep_for(std::chrono::milliseconds(30));
    auto oid = ctx.get<std::string>("order_id").value_or("?");
    std::cout << "[Shipping] Logistics notified for order=" << oid << "\n";
    return taskflow::core::task_state::success;
  }
};

// --- Confirmation task: finalize order and store result ---
struct confirmation_task {
  static constexpr std::string_view name = "confirmation";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto oid = ctx.get<std::string>("order_id").value_or("?");
    ctx.set_result("order_summary", std::string("confirmed:" + oid));
    std::cout << "[Confirmation] Order finalized, result written to result_storage\n";
    return taskflow::core::task_state::success;
  }
};

// --- Async notification task: runs independently in separate blueprint ---
struct async_notify_task {
  static constexpr std::string_view name = "async_notify";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    (void)ctx;
    std::this_thread::sleep_for(std::chrono::milliseconds(80));
    std::cout << "[Async Notification] Push completed (independent blueprint)\n";
    return taskflow::core::task_state::success;
  }
};

// --- Register all tasks to orchestrator ---
static void register_common_tasks(taskflow::engine::orchestrator& orch) {
  orch.register_task<order_validation_task>("order_validation");
  orch.register_task<inventory_in_stock_task>("inventory_ok");
  orch.register_task<inventory_low_task>("inventory_low");
  orch.register_task<procurement_task>("procurement");
  orch.register_task<payment_task>("payment");
  orch.register_task<shipping_task>("shipping");
  orch.register_task<confirmation_task>("confirmation");
  orch.register_task<async_notify_task>("async_notify");
}

// --- Blueprint A: direct in-stock path ---
static taskflow::workflow::workflow_blueprint make_blueprint_in_stock() {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "order_validation"});
  bp.add_node(taskflow::workflow::node_def{2, "inventory_ok"});

  // Payment node with retry configuration
  taskflow::workflow::node_def pay{3, "payment", "payment"};
  pay.retry = taskflow::core::retry_policy{};
  pay.retry->max_attempts = 5;
  pay.retry->initial_delay = std::chrono::milliseconds(15);
  pay.retry->backoff_multiplier = 1.5f;
  bp.add_node(std::move(pay));

  bp.add_node(taskflow::workflow::node_def{4, "shipping"});
  bp.add_node(taskflow::workflow::node_def{5, "confirmation"});

  // Build DAG: 1 -> 2 -> 3 -> 4 -> 5
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 3});
  bp.add_edge(taskflow::workflow::edge_def{3, 4});
  bp.add_edge(taskflow::workflow::edge_def{4, 5});

  return bp;
}

// --- Blueprint B: procurement path for out-of-stock ---
static taskflow::workflow::workflow_blueprint make_blueprint_procurement() {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "order_validation"});
  bp.add_node(taskflow::workflow::node_def{2, "inventory_low"});
  bp.add_node(taskflow::workflow::node_def{6, "procurement"});

  // Payment node with retry configuration (same as above)
  taskflow::workflow::node_def pay{3, "payment", "payment"};
  pay.retry = taskflow::core::retry_policy{};
  pay.retry->max_attempts = 5;
  pay.retry->initial_delay = std::chrono::milliseconds(15);
  pay.retry->backoff_multiplier = 1.5f;
  bp.add_node(std::move(pay));

  bp.add_node(taskflow::workflow::node_def{4, "shipping"});
  bp.add_node(taskflow::workflow::node_def{5, "confirmation"});

  // Build DAG: 1 -> 2 -> 6 -> 3 -> 4 -> 5
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{2, 6});
  bp.add_edge(taskflow::workflow::edge_def{6, 3});
  bp.add_edge(taskflow::workflow::edge_def{3, 4});
  bp.add_edge(taskflow::workflow::edge_def{4, 5});

  return bp;
}

int main() {
  auto results = std::make_unique<taskflow::storage::memory_result_storage>();
  taskflow::storage::memory_result_storage* results_ptr = results.get();

  std::string db_path = "taskflow_ex18_orders.db";
  if (const char* tmp = std::getenv("TMPDIR")) db_path = std::string(tmp) + "/taskflow_ex18_orders.db";

  auto sr = taskflow::storage::state_storage_factory::create("sqlite", db_path);
  if (sr.fell_back_to_memory) {
    std::cout << "State snapshots: sqlite unavailable, using memory backend.\n";
  } else {
    std::cout << "SQLite snapshot: " << db_path << "\n";
  }

  std::unique_ptr<taskflow::engine::orchestrator> orch_ptr =
      std::make_unique<taskflow::engine::orchestrator>(std::make_unique<taskflow::engine::default_thread_pool>(),
                                                       std::move(sr.storage), std::move(results));

  taskflow::engine::orchestrator& orch = *orch_ptr;
  register_common_tasks(orch);

  std::cout << "\n--- Scenario A: In-stock direct flow ---\n";
  orch.register_blueprint(1, make_blueprint_in_stock());
  auto [eid_a, st_a] = orch.run_sync_from_blueprint(1);
  std::cout << "Scenario A status: " << taskflow::core::to_string(st_a) << " exec_id=" << eid_a << "\n";

  // Print stored results
  for (const auto& loc : results_ptr->list(eid_a)) {
    if (auto v = results_ptr->load(loc)) {
      if (auto* s = std::any_cast<std::string>(&*v)) std::cout << "  Result " << loc.key << " = " << *s << "\n";
    }
  }

  std::cout << "\n--- Scenario B: Out-of-stock with procurement ---\n";
  orch.register_blueprint(2, make_blueprint_procurement());
  auto [eid_b, st_b] = orch.run_sync_from_blueprint(2);
  std::cout << "Scenario B status: " << taskflow::core::to_string(st_b) << " exec_id=" << eid_b << "\n";

  if (sr.resolved_backend == "sqlite" && !sr.fell_back_to_memory) {
    auto r2 = taskflow::storage::state_storage_factory::create("sqlite", db_path);
    if (r2.storage) {
      std::cout << "\n--- Exec IDs in persistent storage (reopened db) ---\n";
      for (auto id : r2.storage->list_all()) std::cout << " " << id;
      std::cout << "\n";
    }
  } else {
    std::cout << "\n--- Skipping file storage listing (memory backend) ---\n";
  }

  std::cout << "\n--- Async notification blueprint ---\n";
  taskflow::workflow::workflow_blueprint notify_bp;
  notify_bp.add_node(taskflow::workflow::node_def{1, "async_notify"});
  orch.register_blueprint(99, std::move(notify_bp));
  auto [eid_n, fut] = orch.run_async_from_blueprint(99);
  std::cout << "Async notification submitted exec_id=" << eid_n << "\n";

  if (fut.wait_for(std::chrono::seconds(2)) == std::future_status::ready)
    std::cout << "Async notification completed: " << taskflow::core::to_string(fut.get()) << "\n";
  else
    std::cout << "Wait timeout (unexpected)\n";

  return (st_a == taskflow::core::task_state::success && st_b == taskflow::core::task_state::success) ? 0 : 1;
}
