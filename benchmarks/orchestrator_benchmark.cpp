#include <benchmark/benchmark.h>

#include <string>
#include <taskflow/taskflow.hpp>

namespace {

struct NoopTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) { return taskflow::core::task_state::success; }
};

void BM_OrchestratorLinearChain(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  taskflow::engine::orchestrator orch;
  orch.register_task<NoopTask>("noop");

  taskflow::workflow::workflow_blueprint bp;
  for (int i = 0; i < n; ++i) {
    bp.add_node(taskflow::workflow::node_def{static_cast<std::size_t>(i), "noop"});
    if (i > 0) {
      bp.add_edge(taskflow::workflow::edge_def{static_cast<std::size_t>(i - 1), static_cast<std::size_t>(i)});
    }
  }

  orch.register_blueprint(1, std::move(bp));

  for (auto _ : state) {
    auto [exec_id, result] = orch.run_sync_from_blueprint(1);
    benchmark::DoNotOptimize(exec_id);
    benchmark::DoNotOptimize(result);
  }
}

}  // namespace

BENCHMARK(BM_OrchestratorLinearChain)->Arg(4)->Arg(16);
