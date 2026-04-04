#include <gtest/gtest.h>

#include <atomic>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <vector>

#include <taskflow/taskflow.hpp>
#include <taskflow/storage/memory_result_storage.hpp>

namespace {

// Test fixture for task_ctx
class TaskCtxTest : public ::testing::Test {
 protected:
  void SetUp() override {
    ctx_ = std::make_unique<taskflow::core::task_ctx>();
  }

  std::unique_ptr<taskflow::core::task_ctx> ctx_;
};

// Basic types test
TEST_F(TaskCtxTest, BasicTypes) {
  ctx_->set("int_key", 42);
  ctx_->set("double_key", 3.14);
  ctx_->set("string_key", std::string("hello"));

  EXPECT_EQ(ctx_->get<int>("int_key").value(), 42);
  EXPECT_DOUBLE_EQ(ctx_->get<double>("double_key").value(), 3.14);
  EXPECT_EQ(ctx_->get<std::string>("string_key").value(), "hello");
}

// Custom type with ADL test
struct CustomType {
  int value;
};

std::any to_task_value(const CustomType& ct) { return std::any(ct.value); }

bool from_task_value(const std::any& in, CustomType& ct) {
  if (in.type() == typeid(int)) {
    ct.value = std::any_cast<int>(in);
    return true;
  }
  return false;
}

TEST_F(TaskCtxTest, CustomTypeWithADL) {
  CustomType ct{100};
  ctx_->set("custom_key", ct);

  auto result = ctx_->get<CustomType>("custom_key");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(result->value, 100);
}

// Unsupported custom types fail at compile time (static_assert); see CustomTypeWithADL for the supported path.

// Contains test
TEST_F(TaskCtxTest, Contains) {
  ctx_->set("exists_key", 123);
  EXPECT_TRUE(ctx_->contains("exists_key"));
  EXPECT_FALSE(ctx_->contains("missing_key"));
}

// Data access test
TEST_F(TaskCtxTest, DataAccess) {
  std::unordered_map<std::string, std::any> initial_data = {
    {"key1", 1},
    {"key2", 2.5}
  };
  ctx_->set_data(std::move(initial_data));

  auto& data = ctx_->data();
  EXPECT_EQ(data.size(), 2);
  EXPECT_EQ(std::any_cast<int>(data["key1"]), 1);
  EXPECT_DOUBLE_EQ(std::any_cast<double>(data["key2"]), 2.5);
}

// Progress test
TEST_F(TaskCtxTest, Progress) {
  EXPECT_FLOAT_EQ(ctx_->progress(), 0.0f);

  ctx_->report_progress(0.5f);
  EXPECT_FLOAT_EQ(ctx_->progress(), 0.5f);

  ctx_->report_progress(1.0f);
  EXPECT_FLOAT_EQ(ctx_->progress(), 1.0f);
}

// Cancellation test
TEST_F(TaskCtxTest, Cancellation) {
  EXPECT_FALSE(ctx_->is_cancelled());

  ctx_->cancel();
  EXPECT_TRUE(ctx_->is_cancelled());

  ctx_->set_cancellation_token(taskflow::core::cancellation_token{});
  EXPECT_FALSE(ctx_->get_cancellation_token().is_cancelled());
}

// Node and execution ID test
TEST_F(TaskCtxTest, NodeAndExecId) {
  ctx_->set_node_id(42);
  ctx_->set_exec_id(100);

  EXPECT_EQ(ctx_->node_id(), 42);
  EXPECT_EQ(ctx_->exec_id(), 100);
}

// Execution start time test
TEST_F(TaskCtxTest, ExecStartTime) {
  auto now = std::chrono::system_clock::now();
  ctx_->set_exec_start_time(now);

  EXPECT_EQ(ctx_->exec_start_time(), now);
}

TEST_F(TaskCtxTest, EnsureExecStartTimeSetsOnlyFirst) {
  auto t0 = std::chrono::system_clock::now();
  auto t1 = t0 + std::chrono::hours{3};
  ctx_->ensure_exec_start_time(t0);
  EXPECT_EQ(ctx_->exec_start_time(), t0);
  ctx_->ensure_exec_start_time(t1);
  EXPECT_EQ(ctx_->exec_start_time(), t0);
}

TEST(TaskCtxInvokeScopeTest, NestedScopesInnermostWins) {
  taskflow::core::task_ctx ctx;
  {
    taskflow::core::task_ctx_invoke_scope a(ctx, 10u);
    EXPECT_EQ(ctx.node_id(), 10u);
    {
      taskflow::core::task_ctx_invoke_scope b(ctx, 20u);
      EXPECT_EQ(ctx.node_id(), 20u);
    }
    EXPECT_EQ(ctx.node_id(), 10u);
  }
  EXPECT_EQ(ctx.node_id(), 0u);
}

TEST_F(TaskCtxTest, ResultCollection) {
  taskflow::storage::memory_result_storage storage;
  taskflow::engine::result_collector collector(&storage);
  ctx_->set_collector(&collector);
  ctx_->set_exec_id(7);
  {
    taskflow::core::task_ctx_invoke_scope scope(*ctx_, 3);
    ctx_->set_result("result_key", 42);
  }
  auto result = ctx_->get_result<int>(3, "result_key");
  ASSERT_TRUE(result.has_value());
  EXPECT_EQ(*result, 42);
}

TEST(TaskCtxInvokeScopeTest, ParallelThreadsDistinctNodeIds) {
  taskflow::core::task_ctx ctx;
  ctx.set_exec_id(1);
  constexpr int k_threads = 8;
  std::vector<std::thread> threads;
  threads.reserve(k_threads);
  std::atomic<int> done{0};
  for (int i = 0; i < k_threads; ++i) {
    threads.emplace_back([&, i]() {
      const std::size_t nid = static_cast<std::size_t>(100 + i);
      taskflow::core::task_ctx_invoke_scope scope(ctx, nid);
      EXPECT_EQ(ctx.node_id(), nid);
      const std::string key = "tid_" + std::to_string(i);
      ctx.set(key, static_cast<std::int64_t>(i));
      ASSERT_TRUE(ctx.get<std::int64_t>(key).has_value());
      EXPECT_EQ(ctx.get<std::int64_t>(key).value(), i);
      done.fetch_add(1, std::memory_order_relaxed);
    });
  }
  for (auto& t : threads) {
    t.join();
  }
  EXPECT_EQ(done.load(std::memory_order_relaxed), k_threads);
}

// Move semantics test
TEST_F(TaskCtxTest, MoveSemantics) {
  ctx_->set("move_key", 123);
  auto moved = std::move(*ctx_);

  EXPECT_EQ(moved.get<int>("move_key").value(), 123);
}

}  // namespace
