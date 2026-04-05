#include <gtest/gtest.h>

#include <memory>
#include <stdexcept>

#include <taskflow/engine/executor.hpp>
#include <taskflow/engine/execution.hpp>
#include <taskflow/engine/registry.hpp>
#include <taskflow/workflow/blueprint.hpp>

namespace {

struct OkTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::success;
  }
};

struct FailTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::failed;
  }
};

struct ThrowTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    throw std::runtime_error("boom");
  }
};

struct SetCtxTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx& ctx) {
    ctx.set("k", std::string("v"));
    return taskflow::core::task_state::success;
  }
};

class ExecutorEngineTest : public ::testing::Test {
 protected:
  taskflow::engine::task_registry reg_;
};

TEST_F(ExecutorEngineTest, ExecuteNodeSuccess) {
  reg_.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto st = taskflow::engine::executor::execute_node(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::success);
  EXPECT_EQ(ex.get_node_state(1).state, taskflow::core::task_state::success);
}

TEST_F(ExecutorEngineTest, ExecuteNodeUnknownTypeFails) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "missing_type"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto st = taskflow::engine::executor::execute_node(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::failed);
  EXPECT_EQ(ex.get_node_state(1).state, taskflow::core::task_state::failed);
}

TEST_F(ExecutorEngineTest, ExecuteNodeCancelledBeforeRun) {
  reg_.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.cancel();
  auto st = taskflow::engine::executor::execute_node(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::cancelled);
}

TEST_F(ExecutorEngineTest, ExecuteNodeExceptionBecomesFailed) {
  reg_.register_task<ThrowTask>("throw");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "throw"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto st = taskflow::engine::executor::execute_node(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::failed);
  EXPECT_FALSE(ex.get_node_state(1).error_message.empty());
}

TEST_F(ExecutorEngineTest, ExecuteNodeWritesContextData) {
  reg_.register_task<SetCtxTask>("set");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "set"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  (void)taskflow::engine::executor::execute_node(ex, 1, reg_, {});
  ASSERT_TRUE(ex.context().get<std::string>("k").has_value());
  EXPECT_EQ(*ex.context().get<std::string>("k"), "v");
}

TEST_F(ExecutorEngineTest, ExecuteWithRetrySucceedsAfterFailures) {
  auto counter = std::make_shared<int>(0);
  reg_.register_task("fto", [counter]() {
    return taskflow::core::task_wrapper{[counter](taskflow::core::task_ctx&) {
      (*counter)++;
      if (*counter < 3) return taskflow::core::task_state::failed;
      return taskflow::core::task_state::success;
    }};
  });
  taskflow::workflow::node_def n{1, "fto"};
  taskflow::core::retry_policy pol;
  pol.max_attempts = 5;
  pol.initial_delay = std::chrono::milliseconds(0);
  n.retry = pol;
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(n);
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto st = taskflow::engine::executor::execute_with_retry(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::success);
}

TEST_F(ExecutorEngineTest, ExecuteWithRetryExhausted) {
  reg_.register_task<FailTask>("fail");
  taskflow::workflow::node_def n{1, "fail"};
  taskflow::core::retry_policy pol;
  pol.max_attempts = 2;
  pol.initial_delay = std::chrono::milliseconds(0);
  n.retry = pol;
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(n);
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto st = taskflow::engine::executor::execute_with_retry(ex, 1, reg_, {});
  EXPECT_EQ(st, taskflow::core::task_state::failed);
}

TEST_F(ExecutorEngineTest, IdempotentSkipSecondExecuteNode) {
  reg_.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  EXPECT_EQ(taskflow::engine::executor::execute_node(ex, 1, reg_, {}), taskflow::core::task_state::success);
  EXPECT_EQ(taskflow::engine::executor::execute_node(ex, 1, reg_, {}), taskflow::core::task_state::success);
  EXPECT_EQ(ex.get_node_state(1).state, taskflow::core::task_state::success);
}

}  // namespace
