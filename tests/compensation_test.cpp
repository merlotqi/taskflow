#include <gtest/gtest.h>

#include <string>
#include <taskflow/taskflow.hpp>
#include <vector>

namespace {

struct OkTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) { return taskflow::core::task_state::success; }
};

struct FailTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) { return taskflow::core::task_state::failed; }
};

struct UndoTask {
  explicit UndoTask(std::vector<int>* log, int id) : log_(log), id_(id) {}

  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    if (log_) log_->push_back(id_);
    return taskflow::core::task_state::success;
  }

  std::vector<int>* log_;
  int id_;
};

struct BadUndoTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) { return taskflow::core::task_state::failed; }
};

}  // namespace

TEST(CompensationTest, FailureCompensatesPriorSuccessInReverseOrder) {
  std::vector<int> undo_order;

  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  orch.register_task("undo1", [&undo_order]() { return taskflow::core::task_wrapper{UndoTask{&undo_order, 1}}; });
  orch.register_task("undo2", [&undo_order]() { return taskflow::core::task_wrapper{UndoTask{&undo_order, 2}}; });

  taskflow::workflow::workflow_blueprint bp;
  taskflow::workflow::node_def n1{1, "ok"};
  n1.compensate_task_type = std::string{"undo1"};
  taskflow::workflow::node_def n2{2, "ok"};
  n2.compensate_task_type = std::string{"undo2"};
  taskflow::workflow::node_def n3{3, "fail"};
  bp.add_node(std::move(n1));
  bp.add_node(std::move(n2));
  bp.add_node(std::move(n3));
  bp.add_edge({1, 2});
  bp.add_edge({2, 3});

  orch.register_blueprint(1, std::move(bp));

  taskflow::engine::orchestrator_run_options opts;
  opts.compensate_on_failure = true;
  auto [eid, st] = orch.run_sync_from_blueprint(1, opts);
  EXPECT_EQ(st, taskflow::core::task_state::failed);

  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(1).state, taskflow::core::task_state::compensated);
  EXPECT_EQ(ex->get_node_state(2).state, taskflow::core::task_state::compensated);
  EXPECT_EQ(ex->get_node_state(3).state, taskflow::core::task_state::failed);
  ASSERT_EQ(undo_order.size(), 2u);
  EXPECT_EQ(undo_order[0], 2);
  EXPECT_EQ(undo_order[1], 1);
  EXPECT_TRUE(ex->compensation_phase_completed());
}

TEST(CompensationTest, FailureAfterFirstNodeOnlyCompensatesThatNode) {
  std::vector<int> undo_order;

  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  orch.register_task("undo1", [&undo_order]() { return taskflow::core::task_wrapper{UndoTask{&undo_order, 1}}; });

  taskflow::workflow::workflow_blueprint bp;
  taskflow::workflow::node_def n1{1, "ok"};
  n1.compensate_task_type = std::string{"undo1"};
  bp.add_node(std::move(n1));
  bp.add_node(taskflow::workflow::node_def{2, "fail"});
  bp.add_edge({1, 2});

  orch.register_blueprint(2, std::move(bp));

  taskflow::engine::orchestrator_run_options opts;
  opts.compensate_on_failure = true;
  auto [eid, st] = orch.run_sync_from_blueprint(2, opts);
  EXPECT_EQ(st, taskflow::core::task_state::failed);

  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(1).state, taskflow::core::task_state::compensated);
  EXPECT_EQ(ex->get_node_state(2).state, taskflow::core::task_state::failed);
  ASSERT_EQ(undo_order.size(), 1u);
  EXPECT_EQ(undo_order[0], 1);
}

TEST(CompensationTest, CompensationFailureMarksNode) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  orch.register_task<BadUndoTask>("bad_undo");

  taskflow::workflow::workflow_blueprint bp;
  taskflow::workflow::node_def n1{1, "ok"};
  n1.compensate_task_type = std::string{"bad_undo"};
  bp.add_node(std::move(n1));
  bp.add_node(taskflow::workflow::node_def{2, "fail"});
  bp.add_edge({1, 2});

  orch.register_blueprint(3, std::move(bp));

  taskflow::engine::orchestrator_run_options opts;
  opts.compensate_on_failure = true;
  auto [eid, st] = orch.run_sync_from_blueprint(3, opts);
  EXPECT_EQ(st, taskflow::core::task_state::failed);

  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(1).state, taskflow::core::task_state::compensation_failed);
}

TEST(CompensationTest, DisabledCompensationLeavesSuccess) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  orch.register_task<BadUndoTask>("bad_undo");

  taskflow::workflow::workflow_blueprint bp;
  taskflow::workflow::node_def n1{1, "ok"};
  n1.compensate_task_type = std::string{"bad_undo"};
  bp.add_node(std::move(n1));
  bp.add_node(taskflow::workflow::node_def{2, "fail"});
  bp.add_edge({1, 2});

  orch.register_blueprint(4, std::move(bp));

  auto [eid, st] = orch.run_sync_from_blueprint(4, true);
  EXPECT_EQ(st, taskflow::core::task_state::failed);

  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(1).state, taskflow::core::task_state::success);
  EXPECT_FALSE(ex->compensation_phase_completed());
}
