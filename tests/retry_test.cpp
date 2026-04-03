#include <gtest/gtest.h>

#include <chrono>
#include <taskflow/taskflow.hpp>

using namespace taskflow;

// Test observer to track retry events
class retry_observer : public obs::observer {
 public:
  void on_task_start(std::size_t, std::size_t, std::string_view, std::int32_t) noexcept override {}

  void on_task_complete(std::size_t, std::size_t, std::string_view, std::chrono::milliseconds) noexcept override {}

  void on_task_fail(std::size_t, std::size_t, std::string_view, std::string_view,
                    std::chrono::milliseconds) noexcept override {}

  void on_task_retry(std::size_t, std::size_t, std::string_view, std::int32_t attempt,
                     std::chrono::milliseconds delay_ms) noexcept override {
    retry_attempts.push_back(attempt);
    retry_delays.push_back(delay_ms);
  }

  void on_workflow_complete(std::size_t, core::task_state, std::chrono::milliseconds) noexcept override {}

  std::vector<std::int32_t> retry_attempts;
  std::vector<std::chrono::milliseconds> retry_delays;
};

// Task that fails first N times, then succeeds
struct fail_then_succeed_task {
  static constexpr std::string_view name = "fail_then_succeed";

  core::task_state operator()(core::task_ctx& ctx) {
    auto attempt = ctx.get<std::int64_t>("attempt").value_or(0);
    ctx.set("attempt", attempt + 1);

    if (attempt < fail_count) {
      return core::task_state::failed;
    }
    return core::task_state::success;
  }

  std::int32_t fail_count = 2;
};

// Task that always fails
struct always_fail_task {
  static constexpr std::string_view name = "always_fail";

  core::task_state operator()(core::task_ctx&) { return core::task_state::failed; }
};

TEST(RetryTest, BasicRetry) {
  engine::orchestrator orch;
  orch.register_task<fail_then_succeed_task>("fail_then_succeed");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "fail_then_succeed"};
  core::retry_policy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds{10};
  policy.backoff_multiplier = 1.0f;
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(1, std::move(bp));

  retry_observer obs;
  orch.add_observer(&obs);

  auto [exec_id, state] = orch.run_sync_from_blueprint(1);

  EXPECT_EQ(state, core::task_state::success);
  EXPECT_EQ(obs.retry_attempts.size(), 2);  // Retried twice
  EXPECT_EQ(obs.retry_attempts[0], 2);
  EXPECT_EQ(obs.retry_attempts[1], 3);
}

TEST(RetryTest, MaxAttemptsExceeded) {
  engine::orchestrator orch;
  orch.register_task<always_fail_task>("always_fail");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "always_fail"};
  core::retry_policy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds{10};
  policy.backoff_multiplier = 1.0f;
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(2, std::move(bp));

  auto [exec_id, state] = orch.run_sync_from_blueprint(2);

  EXPECT_EQ(state, core::task_state::failed);

  const auto* exec = orch.get_execution(exec_id);
  ASSERT_NE(exec, nullptr);
  auto ns = exec->get_node_state(1);
  EXPECT_EQ(ns.retry_count, 2);  // Retried 2 times (total 3 attempts)
}

TEST(RetryTest, ExponentialBackoff) {
  engine::orchestrator orch;
  orch.register_task<fail_then_succeed_task>("fail_then_succeed");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "fail_then_succeed"};
  core::retry_policy policy;
  policy.max_attempts = 4;
  policy.initial_delay = std::chrono::milliseconds{100};
  policy.backoff_multiplier = 2.0f;
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(3, std::move(bp));

  retry_observer obs;
  orch.add_observer(&obs);

  auto [exec_id, state] = orch.run_sync_from_blueprint(3);

  EXPECT_EQ(state, core::task_state::success);
  ASSERT_EQ(obs.retry_delays.size(), 2u);

  // First retry: 100ms * 2^0 = 100ms
  EXPECT_GE(obs.retry_delays[0].count(), 90);
  EXPECT_LE(obs.retry_delays[0].count(), 110);

  // Second retry: 100ms * 2^1 = 200ms
  EXPECT_GE(obs.retry_delays[1].count(), 190);
  EXPECT_LE(obs.retry_delays[1].count(), 210);
}

TEST(RetryTest, MaxDelayCap) {
  engine::orchestrator orch;
  orch.register_task<fail_then_succeed_task>("fail_then_succeed");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "fail_then_succeed"};
  core::retry_policy policy;
  policy.max_attempts = 4;
  policy.initial_delay = std::chrono::milliseconds{1000};
  policy.backoff_multiplier = 10.0f;
  policy.max_delay = std::chrono::milliseconds{5000};
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(4, std::move(bp));

  retry_observer obs;
  orch.add_observer(&obs);

  auto [exec_id, state] = orch.run_sync_from_blueprint(4);

  EXPECT_EQ(state, core::task_state::success);
  ASSERT_EQ(obs.retry_delays.size(), 2u);

  // First retry: 1000ms * 10^0
  EXPECT_GE(obs.retry_delays[0].count(), 990);
  EXPECT_LE(obs.retry_delays[0].count(), 1010);

  // Second retry: 1000ms * 10^1 capped to 5000ms
  EXPECT_GE(obs.retry_delays[1].count(), 4990);
  EXPECT_LE(obs.retry_delays[1].count(), 5010);
}

TEST(RetryTest, Jitter) {
  engine::orchestrator orch;
  orch.register_task<fail_then_succeed_task>("fail_then_succeed");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "fail_then_succeed"};
  core::retry_policy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds{100};
  policy.backoff_multiplier = 1.0f;
  policy.jitter = true;
  policy.jitter_range = std::chrono::milliseconds{50};
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(5, std::move(bp));

  retry_observer obs;
  orch.add_observer(&obs);

  auto [exec_id, state] = orch.run_sync_from_blueprint(5);

  EXPECT_EQ(state, core::task_state::success);
  EXPECT_EQ(obs.retry_delays.size(), 2);

  // With jitter, delays should be between 100 and 150ms
  for (auto delay : obs.retry_delays) {
    EXPECT_GE(delay.count(), 90);
    EXPECT_LE(delay.count(), 160);
  }
}

TEST(RetryTest, CustomRetryCondition) {
  engine::orchestrator orch;
  orch.register_task<always_fail_task>("always_fail");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "always_fail"};
  core::retry_policy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds{10};
  policy.backoff_multiplier = 1.0f;
  policy.should_retry = core::retry_conditions::on_timeout_error();
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(6, std::move(bp));

  auto [exec_id, state] = orch.run_sync_from_blueprint(6);

  // Should not retry because error doesn't contain "timeout"
  EXPECT_EQ(state, core::task_state::failed);

  const auto* exec = orch.get_execution(exec_id);
  ASSERT_NE(exec, nullptr);
  auto ns = exec->get_node_state(1);
  EXPECT_EQ(ns.retry_count, 0);  // No retries
}

TEST(RetryTest, RetryStateSet) {
  engine::orchestrator orch;
  orch.register_task<fail_then_succeed_task>("fail_then_succeed");

  workflow::workflow_blueprint bp;
  workflow::node_def node{1, "fail_then_succeed"};
  core::retry_policy policy;
  policy.max_attempts = 3;
  policy.initial_delay = std::chrono::milliseconds{10};
  policy.backoff_multiplier = 1.0f;
  node.retry = policy;
  bp.add_node(node);

  orch.register_blueprint(7, std::move(bp));

  auto [exec_id, state] = orch.run_sync_from_blueprint(7);

  EXPECT_EQ(state, core::task_state::success);

  const auto* exec = orch.get_execution(exec_id);
  ASSERT_NE(exec, nullptr);

  // Check audit log for retry state
  // The state should have been set to retry before each retry attempt
}
