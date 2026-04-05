#include <gtest/gtest.h>

#include <algorithm>
#include <atomic>
#include <string>
#include <vector>

#include <taskflow/integration/event_hooks.hpp>
#include <taskflow/taskflow.hpp>

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

struct WriteOrderTask {
  std::vector<std::string>* log{};
  std::string tag;
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    if (log) log->push_back(tag);
    return taskflow::core::task_state::success;
  }
};

class RecordingObserver : public taskflow::obs::observer {
 public:
  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override {
    (void)exec_id;
    (void)task_type;
    (void)attempt;
    starts_.push_back(node_id);
  }
  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds duration_ms) noexcept override {
    (void)exec_id;
    (void)task_type;
    (void)duration_ms;
    completes_.push_back(node_id);
  }
  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type, std::string_view error,
                    std::chrono::milliseconds duration_ms) noexcept override {
    (void)exec_id;
    (void)task_type;
    (void)error;
    (void)duration_ms;
    fails_.push_back(node_id);
  }
  void on_workflow_complete(std::size_t exec_id, taskflow::core::task_state state,
                            std::chrono::milliseconds duration_ms) noexcept override {
    (void)duration_ms;
    wf_exec_ = exec_id;
    wf_state_ = state;
  }

  std::vector<std::size_t> starts_;
  std::vector<std::size_t> completes_;
  std::vector<std::size_t> fails_;
  std::size_t wf_exec_ = 0;
  taskflow::core::task_state wf_state_ = taskflow::core::task_state::pending;
};

TEST(OrchestratorIntegrationTest, LinearThreeNodesRunsInOrder) {
  std::vector<std::string> order;
  taskflow::engine::orchestrator orch;
  orch.register_task("n1", [&order]() {
    return taskflow::core::task_wrapper{WriteOrderTask{&order, "1"}};
  });
  orch.register_task("n2", [&order]() {
    return taskflow::core::task_wrapper{WriteOrderTask{&order, "2"}};
  });
  orch.register_task("n3", [&order]() {
    return taskflow::core::task_wrapper{WriteOrderTask{&order, "3"}};
  });
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "n1"});
  bp.add_node(taskflow::workflow::node_def{2, "n2"});
  bp.add_node(taskflow::workflow::node_def{3, "n3"});
  bp.add_edge({1, 2});
  bp.add_edge({2, 3});
  orch.register_blueprint(1, std::move(bp));
  auto [eid, st] = orch.run_sync_from_blueprint(1);
  EXPECT_EQ(st, taskflow::core::task_state::success);
  ASSERT_EQ(order.size(), 3u);
  EXPECT_EQ(order[0], "1");
  EXPECT_EQ(order[1], "2");
  EXPECT_EQ(order[2], "3");
  (void)eid;
}

TEST(OrchestratorIntegrationTest, DiamondJoinAllRunBeforeSink) {
  std::atomic<int> count{0};
  taskflow::engine::orchestrator orch;
  orch.register_task("inc", [&count]() {
    return taskflow::core::task_wrapper{[&count](taskflow::core::task_ctx&) {
      count.fetch_add(1);
      return taskflow::core::task_state::success;
    }};
  });
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "inc"});
  bp.add_node(taskflow::workflow::node_def{2, "inc"});
  bp.add_node(taskflow::workflow::node_def{3, "inc"});
  bp.add_edge({1, 3});
  bp.add_edge({2, 3});
  orch.register_blueprint(2, std::move(bp));
  auto st = orch.run_sync(orch.create_execution(2));
  EXPECT_EQ(st, taskflow::core::task_state::success);
  EXPECT_EQ(count.load(), 3);
}

TEST(OrchestratorIntegrationTest, StopOnFirstFailureSkipsDownstream) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  bp.add_node(taskflow::workflow::node_def{2, "fail"});
  bp.add_node(taskflow::workflow::node_def{3, "ok"});
  bp.add_edge({1, 2});
  bp.add_edge({2, 3});
  orch.register_blueprint(3, std::move(bp));
  auto eid = orch.create_execution(3);
  auto st = orch.run_sync(eid, true);
  EXPECT_EQ(st, taskflow::core::task_state::failed);
  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(3).state, taskflow::core::task_state::skipped);
}

TEST(OrchestratorIntegrationTest, ContinueAfterFailureDoesNotSkipPendingByFailurePath) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  orch.register_task<FailTask>("fail");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "fail"});
  bp.add_node(taskflow::workflow::node_def{2, "ok"});
  orch.register_blueprint(4, std::move(bp));
  auto eid = orch.create_execution(4);
  auto st = orch.run_sync(eid, false);
  (void)st;
  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  EXPECT_EQ(ex->get_node_state(2).state, taskflow::core::task_state::success);
}

TEST(OrchestratorIntegrationTest, NamedBlueprintStringView) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint("my_flow", std::move(bp));
  auto eid = orch.create_execution(std::string_view{"my_flow"});
  EXPECT_EQ(orch.run_sync(eid), taskflow::core::task_state::success);
}

TEST(OrchestratorIntegrationTest, ObserverSeesStartCompleteAndWorkflow) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(5, std::move(bp));
  RecordingObserver obs;
  orch.add_observer(&obs);
  auto eid = orch.create_execution(5);
  (void)orch.run_sync(eid);
  ASSERT_FALSE(obs.starts_.empty());
  ASSERT_FALSE(obs.completes_.empty());
  EXPECT_EQ(obs.wf_exec_, eid);
  EXPECT_EQ(obs.wf_state_, taskflow::core::task_state::success);
  orch.remove_observer(&obs);
}

TEST(OrchestratorIntegrationTest, ObserverSeesFailure) {
  taskflow::engine::orchestrator orch;
  orch.register_task<FailTask>("fail");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "fail"});
  orch.register_blueprint(6, std::move(bp));
  RecordingObserver obs;
  orch.add_observer(&obs);
  auto eid = orch.create_execution(6);
  (void)orch.run_sync(eid, false);
  ASSERT_FALSE(obs.fails_.empty());
  EXPECT_EQ(obs.fails_.front(), 1u);
}

TEST(OrchestratorIntegrationTest, EventHooksFire) {
  std::vector<std::string> ev;
  taskflow::integration::workflow_event_hooks hooks;
  hooks.on_node_ready = [&ev](std::size_t, std::size_t nid) { ev.push_back("r" + std::to_string(nid)); };
  hooks.on_node_started = [&ev](std::size_t, std::size_t nid) { ev.push_back("s" + std::to_string(nid)); };
  hooks.on_node_finished = [&ev](std::size_t, std::size_t nid, bool ok) {
    ev.push_back(ok ? ("f" + std::to_string(nid)) : ("x" + std::to_string(nid)));
  };
  hooks.on_workflow_finished = [&ev](std::size_t, bool ok) { ev.push_back(ok ? "wf_ok" : "wf_bad"); };

  taskflow::engine::orchestrator orch;
  orch.set_event_hooks(std::move(hooks));
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(8, std::move(bp));
  (void)orch.run_sync(orch.create_execution(8));

  EXPECT_FALSE(ev.empty());
  EXPECT_NE(std::find(ev.begin(), ev.end(), "wf_ok"), ev.end());
}

TEST(OrchestratorIntegrationTest, SnapshotJsonNonEmptyAfterRun) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(9, std::move(bp));
  auto eid = orch.create_execution(9);
  (void)orch.run_sync(eid);
  const auto* ex = orch.get_execution(eid);
  ASSERT_NE(ex, nullptr);
  auto json = ex->to_snapshot_json();
  EXPECT_FALSE(json.empty());
}

TEST(OrchestratorIntegrationTest, CleanupCompletedRemovesExecution) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(10, std::move(bp));
  auto eid = orch.create_execution(10);
  (void)orch.run_sync(eid);
  EXPECT_EQ(orch.cleanup_completed_executions(), 1u);
  EXPECT_EQ(orch.get_execution(eid), nullptr);
}

TEST(OrchestratorIntegrationTest, MemoryStoragePersistsAcrossSteps) {
  auto mem = std::make_unique<taskflow::storage::memory_state_storage>();
  auto* raw = mem.get();
  taskflow::engine::orchestrator orch(std::move(mem));
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp;
  bp.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(11, std::move(bp));
  auto eid = orch.create_execution(11);
  (void)orch.run_sync(eid);
  auto blob = raw->load(eid);
  ASSERT_TRUE(blob.has_value());
  EXPECT_FALSE(blob->empty());
}

TEST(OrchestratorIntegrationTest, GetBlueprintByIdAndName) {
  taskflow::engine::orchestrator orch;
  orch.register_task<OkTask>("ok");
  taskflow::workflow::workflow_blueprint bp_id;
  bp_id.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint(20, std::move(bp_id));
  taskflow::workflow::workflow_blueprint bp_name;
  bp_name.add_node(taskflow::workflow::node_def{1, "ok"});
  orch.register_blueprint("n20", std::move(bp_name));
  EXPECT_TRUE(orch.has_blueprint(20));
  EXPECT_TRUE(orch.has_blueprint(std::string_view{"n20"}));
  EXPECT_NE(orch.get_blueprint(20), nullptr);
  EXPECT_NE(orch.get_blueprint(std::string_view{"n20"}), nullptr);
}

}  // namespace
