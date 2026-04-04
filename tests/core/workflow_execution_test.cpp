#include <gtest/gtest.h>

#include <taskflow/engine/execution.hpp>
#include <taskflow/workflow/blueprint.hpp>

namespace {

TEST(WorkflowExecutionTest, DefaultConstruction) {
  taskflow::engine::workflow_execution ex;
  EXPECT_EQ(ex.id(), 0u);
  EXPECT_EQ(ex.blueprint(), nullptr);
}

TEST(WorkflowExecutionTest, OverallStateSuccessWhenAllTerminal) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  EXPECT_EQ(ex.overall_state(), taskflow::core::task_state::success);
  EXPECT_TRUE(ex.is_complete());
}

TEST(WorkflowExecutionTest, OverallStateFailedDominates) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  ex.set_node_state(2, taskflow::core::task_state::failed);
  EXPECT_EQ(ex.overall_state(), taskflow::core::task_state::failed);
}

TEST(WorkflowExecutionTest, OverallStatePendingWhenMixed) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  EXPECT_EQ(ex.overall_state(), taskflow::core::task_state::pending);
}

TEST(WorkflowExecutionTest, TryTransitionNodeStateSuccess) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  EXPECT_TRUE(ex.try_transition_node_state(1, taskflow::core::task_state::pending,
                                           taskflow::core::task_state::running));
  EXPECT_EQ(ex.get_node_state(1).state, taskflow::core::task_state::running);
}

TEST(WorkflowExecutionTest, TryTransitionFailsOnMismatch) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  EXPECT_FALSE(ex.try_transition_node_state(1, taskflow::core::task_state::pending,
                                            taskflow::core::task_state::running));
}

TEST(WorkflowExecutionTest, RetryCountIncrements) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  EXPECT_EQ(ex.retry_count(1), 0);
  ex.increment_retry(1);
  ex.increment_retry(1);
  EXPECT_EQ(ex.retry_count(1), 2);
}

TEST(WorkflowExecutionTest, SetNodeError) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_error(1, "e1");
  EXPECT_EQ(ex.get_node_state(1).error_message, "e1");
}

TEST(WorkflowExecutionTest, MarkStartedCompletedTimestamps) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.mark_started();
  EXPECT_NE(ex.start_time(), std::chrono::system_clock::time_point{});
  ex.mark_completed();
  EXPECT_NE(ex.end_time(), std::chrono::system_clock::time_point{});
  EXPECT_GE(ex.end_time(), ex.start_time());
}

TEST(WorkflowExecutionTest, SnapshotJsonContainsExecId) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(42, std::move(bp));
  ex.mark_started();
  auto j = ex.to_snapshot_json();
  EXPECT_NE(j.find("42"), std::string::npos);
}

TEST(WorkflowExecutionTest, CountByState) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  ex.set_node_state(2, taskflow::core::task_state::failed);
  EXPECT_EQ(ex.count_by_state(taskflow::core::task_state::success), 1u);
  EXPECT_EQ(ex.count_by_state(taskflow::core::task_state::failed), 1u);
}

TEST(WorkflowExecutionTest, MovePreservesExecId) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(7, std::move(bp));
  taskflow::engine::workflow_execution ex2 = std::move(ex);
  EXPECT_EQ(ex2.id(), 7u);
}

}  // namespace
