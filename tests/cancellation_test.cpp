#include <gtest/gtest.h>

#include <chrono>
#include <thread>

#include <taskflow/taskflow.hpp>

namespace {

struct SlowTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    for (int i = 0; i < 400 && !ctx.is_cancelled(); ++i) {
      std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    return ctx.is_cancelled() ? taskflow::core::task_state::cancelled : taskflow::core::task_state::success;
  }
};

struct OkTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::success;
  }
};

}  // namespace

TEST(CancellationTest, CancelBeforeRunSyncMarksNodesCancelled) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(1, std::move(bp));
  auto exec_id = orch.create_execution(1);
  ASSERT_TRUE(orch.cancel_execution(exec_id));
  auto st = orch.run_sync(exec_id);
  EXPECT_EQ(st, taskflow::core::task_state::cancelled);
  const auto* ex = orch.get_execution(exec_id);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(1).state, taskflow::core::task_state::cancelled);
}

TEST(CancellationTest, CancelExecutionReturnsFalseForMissingId) {
  taskflow::engine::orchestrator orch;
  EXPECT_FALSE(orch.cancel_execution(99999));
}

TEST(CancellationTest, ConcurrentCancelWhileRunning) {
  taskflow::engine::orchestrator orch;
  orch.register_task<SlowTask>("slow");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "slow"});
  orch.register_blueprint(7, std::move(bp));
  auto exec_id = orch.create_execution(7);

  std::thread runner([&] { (void)orch.run_sync(exec_id); });
  std::this_thread::sleep_for(std::chrono::milliseconds(40));
  orch.cancel_execution(exec_id);
  runner.join();

  const auto* ex = orch.get_execution(exec_id);
  ASSERT_NE(ex, nullptr);
  EXPECT_TRUE(ex->is_cancelled());
}

TEST(CancellationTest, ExecutionTokenReflectsCancelSource) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "x"});
  taskflow::engine::workflow_execution ex(42, std::move(bp));
  auto tok = ex.token();
  EXPECT_FALSE(tok.is_cancelled());
  ex.cancel();
  EXPECT_TRUE(tok.is_cancelled());
}
