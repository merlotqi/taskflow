#include <gtest/gtest.h>

#include <memory>
#include <string>

#include <taskflow/taskflow.hpp>

namespace {

struct OkTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::success;
  }
};

struct SetFlagTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("branch", static_cast<std::int64_t>(1));
    return taskflow::core::task_state::success;
  }
};

}  // namespace

TEST(OrchestratorTest, BlueprintRegisterAndRun) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");

  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(100, std::move(bp));

  EXPECT_TRUE(orch.has_blueprint(100));
  EXPECT_NE(orch.get_blueprint(100), nullptr);

  auto exec_id = orch.create_execution(100);
  auto st = orch.run_sync(exec_id);
  EXPECT_EQ(st, taskflow::core::task_state::success);
}

TEST(OrchestratorTest, StateStorageReceivesSnapshots) {
  auto mem = std::make_unique<taskflow::storage::memory_state_storage>();
  auto* raw = mem.get();
  taskflow::engine::orchestrator orch(std::move(mem));
  orch.register_task<OkTask>("ok");

  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(1, std::move(bp));

  auto exec_id = orch.create_execution(1);
  (void)orch.run_sync(exec_id);

  auto blob = raw->load(exec_id);
  ASSERT_TRUE(blob.has_value());
  EXPECT_FALSE(blob->empty());
}

TEST(OrchestratorSchedulerTest, ConditionalEdgeBlocksSuccessor) {
  taskflow::engine::orchestrator orch;
  orch.register_task<SetFlagTask>("set");
  orch.register_task<OkTask>("ok");

  taskflow::workflow::edge_def blocked{1, 3, [](const taskflow::core::task_ctx&) { return false; }};
  taskflow::workflow::workflow_blueprint bp2;
  bp2.add_node(taskflow::workflow::node_def{1, "set"});
  bp2.add_node(taskflow::workflow::node_def{2, "ok"});
  bp2.add_node(taskflow::workflow::node_def{3, "ok"});
  bp2.add_edge({1, 2});
  bp2.add_edge(std::move(blocked));

  orch.register_blueprint(10, std::move(bp2));
  auto exec_id = orch.create_execution(10);
  (void)orch.run_sync(exec_id, false);

  const auto* ex = orch.get_execution(exec_id);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(2).state, taskflow::core::task_state::success);
  EXPECT_EQ(ex->get_node_state(3).state, taskflow::core::task_state::pending);
}

TEST(OrchestratorSchedulerTest, ConditionalEdgeAllowsSuccessor) {
  taskflow::engine::orchestrator orch;
  orch.register_task<SetFlagTask>("set");
  orch.register_task<OkTask>("ok");

  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "set"});
  bp.add_node(taskflow::workflow::node_def{2, "ok"});
  bp.add_edge(taskflow::workflow::edge_def{
      1, 2, [](const taskflow::core::task_ctx& ctx) {
        auto b = ctx.get<std::int64_t>("branch");
        return b && *b != 0;
      }});

  orch.register_blueprint(11, std::move(bp));
  auto exec_id = orch.create_execution(11);
  auto st = orch.run_sync(exec_id);
  EXPECT_EQ(st, taskflow::core::task_state::success);
}

TEST(RegistryFactoryTest, StatefulFactoryViaStdFunction) {
  taskflow::engine::task_registry reg;
  int counter = 0;
  reg.register_task("cnt", [&counter]() {
    return taskflow::core::task_wrapper{[&counter](taskflow::core::task_ctx&) {
      ++counter;
      return taskflow::core::task_state::success;
    }};
  });
  auto t = reg.create("cnt");
  ASSERT_TRUE(static_cast<bool>(t));
  taskflow::core::task_ctx ctx;
  EXPECT_EQ(t.execute(ctx), taskflow::core::task_state::success);
  EXPECT_EQ(counter, 1);
}
