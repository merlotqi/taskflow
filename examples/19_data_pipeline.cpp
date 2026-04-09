/**
 * Example 19: Data Processing Pipeline — Log/Event Stream
 *
 * Demonstrates:
 *  - Coordinator node writes batch metadata
 *  - Multiple parallel ingest nodes simulating different log sources and levels
 *  - Cleanup node filters (discards levels below threshold)
 *  - Aggregate node performs statistics
 *  - Final set_result outputs summary
 *  - Linear aggregation to avoid "OR join" pending issues that can occur with
 *    conditional edges
 */

#include <iostream>
#include <memory>
#include <string>
#include <taskflow/storage/memory_result_storage.hpp>
#include <taskflow/taskflow.hpp>

// --- Coordinator task: initializes batch context ---
struct coord_task {
  static constexpr std::string_view name = "coord";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("batch_id", std::string("B-2026-0404"));
    ctx.set("level_threshold", static_cast<int64_t>(1));  // Keep only level >= 1
    std::cout << "[Coordinator] Batch ready\n";
    return taskflow::core::task_state::success;
  }
};

// --- Ingest task: simulates data source ingestion ---
struct ingest_task {
  int source_index = 0;
  int64_t level = 0;
  static constexpr std::string_view name = "ingest";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("src_" + std::to_string(source_index) + "_level", level);
    ctx.set("src_" + std::to_string(source_index) + "_lines", static_cast<int64_t>(10 + source_index * 3));
    return taskflow::core::task_state::success;
  }
};

// --- Cleanse task: filters data based on level threshold ---
struct cleanse_task {
  static constexpr std::string_view name = "cleanse";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    const int64_t thr = ctx.get<int64_t>("level_threshold").value_or(0);
    int64_t kept = 0;
    std::cout << "[Ingest] Three sources completed (summarized in cleanse node)\n";

    // Process each source and filter based on level threshold
    for (int i = 0; i < 3; ++i) {
      auto lv = ctx.get<int64_t>("src_" + std::to_string(i) + "_level").value_or(-1);
      if (lv >= thr) {
        kept += ctx.get<int64_t>("src_" + std::to_string(i) + "_lines").value_or(0);
        std::cout << "[Cleanse] Kept source " << i << " (level=" << lv << ")\n";
      } else {
        ctx.set("src_" + std::to_string(i) + "_lines", static_cast<int64_t>(0));
        std::cout << "[Cleanse] Discarded source " << i << " (level=" << lv << ")\n";
      }
    }
    ctx.set("kept_lines", kept);
    return taskflow::core::task_state::success;
  }
};

// --- Aggregate task: calculates statistics ---
struct aggregate_task {
  static constexpr std::string_view name = "aggregate";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    int64_t total = ctx.get<int64_t>("kept_lines").value_or(0);
    ctx.set("total_lines", total);
    std::cout << "[Aggregate] total_lines=" << total << "\n";
    return taskflow::core::task_state::success;
  }
};

// --- Sink task: final output to result storage ---
struct sink_task {
  static constexpr std::string_view name = "sink";

  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    auto batch = ctx.get<std::string>("batch_id").value_or("?");
    auto total = ctx.get<int64_t>("total_lines").value_or(0);
    ctx.set_result("pipeline_report", std::string("batch=" + batch + " lines=" + std::to_string(total)));
    std::cout << "[Sink] Result written to result_storage\n";
    return taskflow::core::task_state::success;
  }
};

int main() {
  auto mem = std::make_unique<taskflow::storage::memory_result_storage>();
  taskflow::engine::orchestrator orch(std::move(mem));

  // Register tasks with orchestrator
  orch.register_task<coord_task>("coord");
  orch.register_task("ingest", []() { return taskflow::core::task_wrapper{ingest_task{0, 2}}; });
  orch.register_task("ingest_b", []() { return taskflow::core::task_wrapper{ingest_task{1, 0}}; });
  orch.register_task("ingest_c", []() { return taskflow::core::task_wrapper{ingest_task{2, 1}}; });
  orch.register_task<cleanse_task>("cleanse");
  orch.register_task<aggregate_task>("aggregate");
  orch.register_task<sink_task>("sink");

  // Build DAG:
  //        1 (coord)
  //     /  |  \'
  //    2   3   4 (ingest sources)
  //     \  |  /
  //        5 (cleanse) -> 6 (aggregate) -> 7 (sink)
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "coord"});
  bp.add_node(taskflow::workflow::node_def{2, "ingest"});
  bp.add_node(taskflow::workflow::node_def{3, "ingest_b"});
  bp.add_node(taskflow::workflow::node_def{4, "ingest_c"});
  bp.add_node(taskflow::workflow::node_def{5, "cleanse"});
  bp.add_node(taskflow::workflow::node_def{6, "aggregate"});
  bp.add_node(taskflow::workflow::node_def{7, "sink"});

  // Define dependencies
  bp.add_edge(taskflow::workflow::edge_def{1, 2});
  bp.add_edge(taskflow::workflow::edge_def{1, 3});
  bp.add_edge(taskflow::workflow::edge_def{1, 4});
  bp.add_edge(taskflow::workflow::edge_def{2, 5});
  bp.add_edge(taskflow::workflow::edge_def{3, 5});
  bp.add_edge(taskflow::workflow::edge_def{4, 5});
  bp.add_edge(taskflow::workflow::edge_def{5, 6});
  bp.add_edge(taskflow::workflow::edge_def{6, 7});

  orch.register_blueprint(1, std::move(bp));

  std::cout << "=== Example 19: Data Pipeline ===\n";
  auto [eid, st] = orch.run_sync_from_blueprint(1);
  std::cout << "Completed: " << taskflow::core::to_string(st) << " exec_id=" << eid << "\n";

  return st == taskflow::core::task_state::success ? 0 : 1;
}
