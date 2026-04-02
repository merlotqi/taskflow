#include <taskflow/taskflow.hpp>

#include <benchmark/benchmark.h>

#include <memory>
#include <string>

namespace {

struct NoopTask : tf::TaskBase {
  void execute(tf::TaskCtx&) override {}
};

void BM_OrchestratorLinearChain(benchmark::State& state) {
  const int n = static_cast<int>(state.range(0));
  tf::Orchestrator orch;
  orch.register_task_type("noop", [] { return tf::TaskPtr{std::make_unique<NoopTask>()}; });

  tf::WorkflowBlueprint bp;
  for (int i = 0; i < n; ++i) {
    bp.add_node({std::string("n") + std::to_string(i), "noop"});
    if (i > 0) {
      bp.add_edge({std::string("n") + std::to_string(i - 1), std::string("n") + std::to_string(i)});
    }
  }

  for (auto _ : state) {
    tf::ExecutionId id = orch.create_execution(bp);
    orch.run_sync(id);
    benchmark::DoNotOptimize(id);
  }
}

}  // namespace

BENCHMARK(BM_OrchestratorLinearChain)->Arg(4)->Arg(16);
