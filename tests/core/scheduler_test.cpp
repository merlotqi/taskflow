#include <gtest/gtest.h>

#include <algorithm>

#include <taskflow/engine/execution.hpp>
#include <taskflow/engine/scheduler.hpp>
#include <taskflow/workflow/blueprint.hpp>

namespace {

class EngineSchedulerTest : public ::testing::Test {};

TEST_F(EngineSchedulerTest, RootPendingIsReady) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  ASSERT_EQ(ready.size(), 1u);
  EXPECT_EQ(ready[0], 1u);
}

TEST_F(EngineSchedulerTest, AfterPredecessorSuccessSuccessorReady) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  ASSERT_EQ(ready.size(), 1u);
  EXPECT_EQ(ready[0], 2u);
}

TEST_F(EngineSchedulerTest, PickNextReturnsFirstReadyOrZero) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(9, std::move(bp));
  EXPECT_EQ(taskflow::engine::scheduler::pick_next(ex), 1u);
  ex.set_node_state(1, taskflow::core::task_state::success);
  EXPECT_EQ(taskflow::engine::scheduler::pick_next(ex), 0u);
}

TEST_F(EngineSchedulerTest, ReadyNodesOrderedMatchesReadyNodes) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto a = taskflow::engine::scheduler::ready_nodes(ex);
  auto b = taskflow::engine::scheduler::ready_nodes_ordered(ex);
  EXPECT_EQ(a, b);
}

TEST_F(EngineSchedulerTest, HasPendingDetectsPendingRunningRetry) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  EXPECT_TRUE(taskflow::engine::scheduler::has_pending(ex));
  ex.set_node_state(1, taskflow::core::task_state::success);
  EXPECT_FALSE(taskflow::engine::scheduler::has_pending(ex));

  taskflow::workflow::workflow_blueprint bp2;
  bp2.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex2(2, std::move(bp2));
  ex2.set_node_state(1, taskflow::core::task_state::running);
  EXPECT_TRUE(taskflow::engine::scheduler::has_pending(ex2));
}

TEST_F(EngineSchedulerTest, FailedPredecessorBlocksSuccessor) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::failed);
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  EXPECT_TRUE(ready.empty());
}

TEST_F(EngineSchedulerTest, TwoRootsBothReadyWhenPending) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_node(taskflow::workflow::node_def{3, "c"});
  bp.add_edge({1, 3});
  bp.add_edge({2, 3});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  ASSERT_EQ(ready.size(), 2u);
  EXPECT_NE(std::find(ready.begin(), ready.end(), 1u), ready.end());
  EXPECT_NE(std::find(ready.begin(), ready.end(), 2u), ready.end());
}

TEST_F(EngineSchedulerTest, ConditionFalseBlocksWhenPredecessorSucceeded) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2, [](const taskflow::core::task_ctx&) { return false; }});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  EXPECT_TRUE(ready.empty());
}

TEST_F(EngineSchedulerTest, ConditionTrueAllowsSuccessor) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  bp.add_node(taskflow::workflow::node_def{2, "b"});
  bp.add_edge({1, 2, [](const taskflow::core::task_ctx&) { return true; }});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::success);
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  ASSERT_EQ(ready.size(), 1u);
  EXPECT_EQ(ready[0], 2u);
}

TEST_F(EngineSchedulerTest, RetryStateIsEligibleLikePending) {
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "a"});
  taskflow::engine::workflow_execution ex(1, std::move(bp));
  ex.set_node_state(1, taskflow::core::task_state::retry);
  auto ready = taskflow::engine::scheduler::ready_nodes(ex);
  ASSERT_EQ(ready.size(), 1u);
  EXPECT_EQ(ready[0], 1u);
}

}  // namespace
