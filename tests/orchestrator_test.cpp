#include <taskflow/taskflow.hpp>

#include <gtest/gtest.h>

#include <memory>

namespace {

struct NoopTask : tf::TaskBase {
  void execute(tf::TaskCtx&) override {}
};

struct SetKeyTask : tf::TaskBase {
  explicit SetKeyTask(std::string k, std::string v) : key(std::move(k)), value(std::move(v)) {}
  std::string key;
  std::string value;
  void execute(tf::TaskCtx& ctx) override { ctx.set(key, nlohmann::json(value)); }
};

struct FailTask : tf::TaskBase {
  void execute(tf::TaskCtx&) override { throw std::runtime_error("boom"); }
};

}  // namespace

TEST(BlueprintTest, TopologicalOrderLinear) {
  tf::WorkflowBlueprint bp;
  bp.add_node({"a", "noop"});
  bp.add_node({"b", "noop"});
  bp.add_edge({"a", "b"});
  auto order = bp.topological_order();
  ASSERT_EQ(order.size(), 2u);
  EXPECT_EQ(order[0], "a");
  EXPECT_EQ(order[1], "b");
}

TEST(BlueprintTest, CycleThrows) {
  tf::WorkflowBlueprint bp;
  bp.add_node({"a", "noop"});
  bp.add_node({"b", "noop"});
  bp.add_edge({"a", "b"});
  bp.add_edge({"b", "a"});
  EXPECT_THROW(bp.validate_acyclic(), std::runtime_error);
}

TEST(OrchestratorTest, SingleNodeRuns) {
  tf::Orchestrator orch;
  orch.register_task_type("noop", [] { return tf::TaskPtr{std::make_unique<NoopTask>()}; });

  tf::WorkflowBlueprint bp;
  bp.add_node({"only", "noop"});
  tf::ExecutionId id = orch.create_execution(bp);
  orch.run_sync(id);
  const tf::WorkflowExecution* ex = orch.get_execution(id);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->state_of("only"), tf::TaskState::Success);
}

TEST(OrchestratorTest, ChainPassesContext) {
  tf::Orchestrator orch;
  orch.register_task_type("noop", [] { return tf::TaskPtr{std::make_unique<NoopTask>()}; });
  orch.register_task_type("set", [] { return tf::TaskPtr{std::make_unique<SetKeyTask>("k", "v")}; });

  tf::WorkflowBlueprint bp;
  bp.add_node({"a", "set"});
  bp.add_node({"b", "noop"});
  bp.add_edge({"a", "b"});
  tf::ExecutionId id = orch.create_execution(bp);
  orch.run_sync(id);
  const tf::WorkflowExecution* ex = orch.get_execution(id);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->state_of("a"), tf::TaskState::Success);
  EXPECT_EQ(ex->state_of("b"), tf::TaskState::Success);
  EXPECT_EQ(ex->context.get("k").get<std::string>(), "v");
}

TEST(OrchestratorTest, FailureStopsSuccess) {
  tf::Orchestrator orch;
  orch.register_task_type("fail", [] { return tf::TaskPtr{std::make_unique<FailTask>()}; });
  orch.register_task_type("noop", [] { return tf::TaskPtr{std::make_unique<NoopTask>()}; });

  tf::WorkflowBlueprint bp;
  bp.add_node({"a", "fail"});
  bp.add_node({"b", "noop"});
  bp.add_edge({"a", "b"});
  tf::ExecutionId id = orch.create_execution(bp);
  orch.run_sync(id);
  const tf::WorkflowExecution* ex = orch.get_execution(id);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->state_of("a"), tf::TaskState::Failed);
  EXPECT_EQ(ex->state_of("b"), tf::TaskState::Pending);
}

TEST(SerializerTest, RoundTripBlueprint) {
  tf::WorkflowBlueprint bp;
  bp.add_node({"x", "t"});
  bp.add_node({"y", "t2"});
  bp.add_edge({"x", "y"});
  std::string s = tf::blueprint_to_string(bp);
  tf::WorkflowBlueprint bp2 = tf::blueprint_from_string(s);
  ASSERT_EQ(bp2.nodes().size(), 2u);
  ASSERT_NE(bp2.find_node("x"), nullptr);
  EXPECT_EQ(bp2.find_node("x")->task_type, "t");
  ASSERT_NE(bp2.find_node("y"), nullptr);
  EXPECT_EQ(bp2.find_node("y")->task_type, "t2");
}
