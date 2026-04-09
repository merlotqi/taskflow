#include <gtest/gtest.h>
#include <taskflow_c.h>

#include <chrono>
#include <string>
#include <thread>

namespace {

std::string TakeOwnedString(char* value, size_t size) {
  std::string out = value ? std::string(value, size) : std::string{};
  tf_string_free(value);
  return out;
}

int SeedTask(tf_task_context_t ctx, void* /*userdata*/) {
  int64_t base = 0;
  if (tf_task_context_get_int64(ctx, "base", &base) != TF_OK) {
    (void)tf_task_context_set_error(ctx, "missing base");
    return TF_TASK_STATE_FAILED;
  }
  if (tf_task_context_set_int64(ctx, "value", base + 1) != TF_OK) {
    return TF_TASK_STATE_FAILED;
  }
  if (tf_task_context_set_result_int64(ctx, "seed", base + 1) != TF_OK) {
    return TF_TASK_STATE_FAILED;
  }
  return TF_TASK_STATE_SUCCESS;
}

int ConsumeTask(tf_task_context_t ctx, void* /*userdata*/) {
  int64_t seed = 0;
  if (tf_task_context_get_result_int64(ctx, 1, "seed", &seed) != TF_OK) {
    (void)tf_task_context_set_error(ctx, "missing seed result");
    return TF_TASK_STATE_FAILED;
  }
  if (seed != 21) {
    (void)tf_task_context_set_error(ctx, "unexpected seed result");
    return TF_TASK_STATE_FAILED;
  }
  if (tf_task_context_set_string(ctx, "status", "done") != TF_OK) {
    return TF_TASK_STATE_FAILED;
  }
  return TF_TASK_STATE_SUCCESS;
}

int NoopTask(tf_task_context_t /*ctx*/, void* /*userdata*/) { return TF_TASK_STATE_SUCCESS; }

int ConditionalEdge(tf_task_context_t ctx, void* /*userdata*/) {
  int allow_downstream = 0;
  if (tf_task_context_get_bool(ctx, "allow_downstream", &allow_downstream) != TF_OK) {
    return 0;
  }
  return allow_downstream;
}

struct SleepTaskConfig {
  int sleep_ms = 0;
};

int SleepTask(tf_task_context_t ctx, void* userdata) {
  auto* config = static_cast<SleepTaskConfig*>(userdata);
  std::this_thread::sleep_for(std::chrono::milliseconds(config ? config->sleep_ms : 0));
  return tf_task_context_set_double(ctx, "elapsed", 1.0) == TF_OK ? TF_TASK_STATE_SUCCESS : TF_TASK_STATE_FAILED;
}

TEST(TaskflowCApiTest, ProgrammaticBlueprintAndCallbacksWorkEndToEnd) {
  TF_OrchestratorOptions orchestrator_options{};
  tf_orchestrator_options_init(&orchestrator_options);
  orchestrator_options.enable_memory_result_storage = 1;

  tf_orchestrator_t orchestrator = 0;
  ASSERT_EQ(tf_orchestrator_create_ex(&orchestrator_options, &orchestrator), TF_OK);

  tf_blueprint_t blueprint = 0;
  ASSERT_EQ(tf_blueprint_create(&blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 1, "seed", "Seed"), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 2, "consume", "Consume"), TF_OK);
  ASSERT_EQ(tf_blueprint_add_edge(blueprint, 1, 2), TF_OK);

  TF_RetryPolicy retry_policy{};
  tf_retry_policy_init(&retry_policy);
  retry_policy.max_attempts = 2;
  ASSERT_EQ(tf_blueprint_set_node_retry(blueprint, 2, &retry_policy), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node_tag(blueprint, 2, "sink"), TF_OK);

  int has_node = 0;
  ASSERT_EQ(tf_blueprint_has_node(blueprint, 2, &has_node), TF_OK);
  EXPECT_EQ(has_node, 1);

  size_t node_count = 0;
  size_t edge_count = 0;
  ASSERT_EQ(tf_blueprint_get_node_count(blueprint, &node_count), TF_OK);
  ASSERT_EQ(tf_blueprint_get_edge_count(blueprint, &edge_count), TF_OK);
  EXPECT_EQ(node_count, 2u);
  EXPECT_EQ(edge_count, 1u);

  char* validation = nullptr;
  size_t validation_size = 0;
  ASSERT_EQ(tf_blueprint_validate_copy(blueprint, &validation, &validation_size), TF_OK);
  EXPECT_TRUE(TakeOwnedString(validation, validation_size).empty());

  ASSERT_EQ(tf_orchestrator_register_task_callback(orchestrator, "seed", SeedTask, nullptr), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_task_callback(orchestrator, "consume", ConsumeTask, nullptr), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_blueprint(orchestrator, 100, blueprint), TF_OK);

  ASSERT_EQ(tf_blueprint_destroy(blueprint), TF_OK);

  tf_execution_t execution = 0;
  ASSERT_EQ(tf_orchestrator_create_execution(orchestrator, 100, &execution), TF_OK);
  ASSERT_EQ(tf_execution_context_set_int64(execution, "base", 20), TF_OK);

  int is_ready = 1;
  int polled_state = TF_TASK_STATE_SUCCESS;
  ASSERT_EQ(tf_execution_poll(execution, &is_ready, &polled_state), TF_OK);
  EXPECT_EQ(is_ready, 0);

  int final_state = TF_TASK_STATE_PENDING;
  ASSERT_EQ(tf_execution_run_sync(execution, nullptr, &final_state), TF_OK);
  EXPECT_EQ(final_state, TF_TASK_STATE_SUCCESS);

  int overall_state = TF_TASK_STATE_PENDING;
  ASSERT_EQ(tf_execution_get_overall_state(execution, &overall_state), TF_OK);
  EXPECT_EQ(overall_state, TF_TASK_STATE_SUCCESS);

  int node_state = TF_TASK_STATE_PENDING;
  ASSERT_EQ(tf_execution_get_node_state(execution, 1, &node_state), TF_OK);
  EXPECT_EQ(node_state, TF_TASK_STATE_SUCCESS);
  ASSERT_EQ(tf_execution_get_node_state(execution, 2, &node_state), TF_OK);
  EXPECT_EQ(node_state, TF_TASK_STATE_SUCCESS);

  size_t success_count = 0;
  ASSERT_EQ(tf_execution_count_by_state(execution, TF_TASK_STATE_SUCCESS, &success_count), TF_OK);
  EXPECT_EQ(success_count, 2u);

  int64_t value = 0;
  ASSERT_EQ(tf_execution_context_get_int64(execution, "value", &value), TF_OK);
  EXPECT_EQ(value, 21);

  char* status = nullptr;
  size_t status_size = 0;
  ASSERT_EQ(tf_execution_context_get_string_copy(execution, "status", &status, &status_size), TF_OK);
  EXPECT_EQ(TakeOwnedString(status, status_size), "done");

  int64_t seed = 0;
  ASSERT_EQ(tf_execution_result_get_int64(execution, 1, "seed", &seed), TF_OK);
  EXPECT_EQ(seed, 21);

  char* snapshot = nullptr;
  size_t snapshot_size = 0;
  ASSERT_EQ(tf_execution_snapshot_json_copy(execution, &snapshot, &snapshot_size), TF_OK);
  EXPECT_FALSE(TakeOwnedString(snapshot, snapshot_size).empty());

  int64_t started_ms = 0;
  int64_t ended_ms = 0;
  ASSERT_EQ(tf_execution_get_start_time_epoch_ms(execution, &started_ms), TF_OK);
  ASSERT_EQ(tf_execution_get_end_time_epoch_ms(execution, &ended_ms), TF_OK);
  EXPECT_GT(started_ms, 0);
  EXPECT_GE(ended_ms, started_ms);

  ASSERT_EQ(tf_execution_destroy(execution), TF_OK);
  ASSERT_EQ(tf_orchestrator_destroy(orchestrator), TF_OK);
}

TEST(TaskflowCApiTest, ConditionalEdgeCanBeDrivenFromExecutionContext) {
  tf_orchestrator_t orchestrator = 0;
  ASSERT_EQ(tf_orchestrator_create(&orchestrator), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_task_callback(orchestrator, "noop", NoopTask, nullptr), TF_OK);

  tf_blueprint_t blueprint = 0;
  ASSERT_EQ(tf_blueprint_create(&blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 1, "noop", nullptr), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 2, "noop", nullptr), TF_OK);
  ASSERT_EQ(tf_blueprint_add_conditional_edge(blueprint, 1, 2, ConditionalEdge, nullptr), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_blueprint(orchestrator, 200, blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_destroy(blueprint), TF_OK);

  tf_execution_t execution = 0;
  ASSERT_EQ(tf_orchestrator_create_execution(orchestrator, 200, &execution), TF_OK);
  ASSERT_EQ(tf_execution_context_set_bool(execution, "allow_downstream", 0), TF_OK);

  int final_state = TF_TASK_STATE_SUCCESS;
  ASSERT_EQ(tf_execution_run_sync(execution, nullptr, &final_state), TF_OK);
  EXPECT_EQ(final_state, TF_TASK_STATE_PENDING);

  int node_state = TF_TASK_STATE_SUCCESS;
  ASSERT_EQ(tf_execution_get_node_state(execution, 1, &node_state), TF_OK);
  EXPECT_EQ(node_state, TF_TASK_STATE_SUCCESS);
  ASSERT_EQ(tf_execution_get_node_state(execution, 2, &node_state), TF_OK);
  EXPECT_EQ(node_state, TF_TASK_STATE_PENDING);

  ASSERT_EQ(tf_execution_context_set_bool(execution, "allow_downstream", 1), TF_OK);
  ASSERT_EQ(tf_execution_run_sync(execution, nullptr, &final_state), TF_OK);
  EXPECT_EQ(final_state, TF_TASK_STATE_SUCCESS);

  ASSERT_EQ(tf_execution_destroy(execution), TF_OK);
  ASSERT_EQ(tf_orchestrator_destroy(orchestrator), TF_OK);
}

TEST(TaskflowCApiTest, AsyncExecutionCanBePolledAndWaited) {
  tf_orchestrator_t orchestrator = 0;
  ASSERT_EQ(tf_orchestrator_create(&orchestrator), TF_OK);

  SleepTaskConfig config;
  config.sleep_ms = 250;
  ASSERT_EQ(tf_orchestrator_register_task_callback(orchestrator, "sleep", SleepTask, &config), TF_OK);

  tf_blueprint_t blueprint = 0;
  ASSERT_EQ(tf_blueprint_create(&blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 1, "sleep", nullptr), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_blueprint(orchestrator, 300, blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_destroy(blueprint), TF_OK);

  tf_execution_t execution = 0;
  ASSERT_EQ(tf_orchestrator_run_async(orchestrator, 300, nullptr, &execution), TF_OK);

  int is_ready = 1;
  int state = TF_TASK_STATE_SUCCESS;
  ASSERT_EQ(tf_execution_poll(execution, &is_ready, &state), TF_OK);
  EXPECT_EQ(is_ready, 0);

  ASSERT_EQ(tf_execution_wait(execution, &state), TF_OK);
  EXPECT_EQ(state, TF_TASK_STATE_SUCCESS);

  ASSERT_EQ(tf_execution_poll(execution, &is_ready, &state), TF_OK);
  EXPECT_EQ(is_ready, 1);
  EXPECT_EQ(state, TF_TASK_STATE_SUCCESS);

  ASSERT_EQ(tf_execution_destroy(execution), TF_OK);
  ASSERT_EQ(tf_orchestrator_destroy(orchestrator), TF_OK);
}

TEST(TaskflowCApiTest, CleanupCompletedExecutionsPrunesExternalHandles) {
  tf_orchestrator_t orchestrator = 0;
  ASSERT_EQ(tf_orchestrator_create(&orchestrator), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_task_callback(orchestrator, "noop", NoopTask, nullptr), TF_OK);

  tf_blueprint_t blueprint = 0;
  ASSERT_EQ(tf_blueprint_create(&blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_add_node(blueprint, 1, "noop", nullptr), TF_OK);
  ASSERT_EQ(tf_orchestrator_register_blueprint(orchestrator, 400, blueprint), TF_OK);
  ASSERT_EQ(tf_blueprint_destroy(blueprint), TF_OK);

  tf_execution_t execution = 0;
  ASSERT_EQ(tf_orchestrator_run_sync(orchestrator, 400, nullptr, &execution), TF_OK);

  size_t cleaned = 0;
  ASSERT_EQ(tf_orchestrator_cleanup_completed_executions(orchestrator, &cleaned), TF_OK);
  EXPECT_EQ(cleaned, 1u);

  int overall_state = TF_TASK_STATE_PENDING;
  EXPECT_EQ(tf_execution_get_overall_state(execution, &overall_state), TF_ERROR_EXECUTION_NOT_FOUND);

  ASSERT_EQ(tf_orchestrator_destroy(orchestrator), TF_OK);
}

}  // namespace
