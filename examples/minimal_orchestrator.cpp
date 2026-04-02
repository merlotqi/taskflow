#include <taskflow/taskflow.hpp>

#include <iostream>
#include <memory>

struct HelloTask : tf::TaskBase {
  void execute(tf::TaskCtx& ctx) override {
    ctx.set("msg", nlohmann::json(std::string("hello from orchestrator")));
  }
};

int main() {
  tf::Orchestrator orch;
  orch.register_task_type("hello", [] { return tf::TaskPtr{std::make_unique<HelloTask>()}; });

  tf::WorkflowBlueprint bp;
  bp.add_node({"step1", "hello"});

  tf::ExecutionId id = orch.create_execution(bp);
  orch.run_sync(id);

  const tf::WorkflowExecution* ex = orch.get_execution(id);
  if (!ex) {
    std::cerr << "execution missing\n";
    return 1;
  }
  std::cout << ex->context.get("msg").get<std::string>() << '\n';
  return 0;
}
