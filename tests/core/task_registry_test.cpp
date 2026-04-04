#include <gtest/gtest.h>

#include <memory>
#include <string>
#include <unordered_map>

#include <taskflow/taskflow.hpp>

namespace {

// Test fixture for task_registry
class CoreTaskRegistryTest : public ::testing::Test {
 protected:
   void SetUp() override {
     registry_ = std::make_unique<taskflow::engine::task_registry>();
   }

   std::unique_ptr<taskflow::engine::task_registry> registry_;
};

// Simple task for testing
struct SimpleTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::success;
  }
};

// Task that captures state
struct StatefulTask {
  int value;

  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::success;
  }
};

// Task that returns different states
struct StateTask {
  taskflow::core::task_state operator()(taskflow::core::task_ctx&) {
    return taskflow::core::task_state::failed;
  }
};

// Test task registration by type
TEST_F(CoreTaskRegistryTest, RegisterByType) {
  registry_->register_task<SimpleTask>("simple");

  auto task = registry_->create("simple");
  ASSERT_TRUE(static_cast<bool>(task));
  EXPECT_NE(std::string(task.type_name()).find("SimpleTask"), std::string::npos);
}

// Test task registration by factory
TEST_F(CoreTaskRegistryTest, RegisterByFactory) {
  registry_->register_task("stateful", []() {
    return taskflow::core::task_wrapper{StatefulTask{42}};
  });

  auto task = registry_->create("stateful");
  ASSERT_TRUE(static_cast<bool>(task));
  EXPECT_NE(std::string(task.type_name()).find("StatefulTask"), std::string::npos);
}

// Test create non-existent task
TEST_F(CoreTaskRegistryTest, CreateNonExistent) {
  auto task = registry_->create("nonexistent");
  EXPECT_FALSE(static_cast<bool>(task));
}

// Test multiple registrations
TEST_F(CoreTaskRegistryTest, MultipleRegistrations) {
  registry_->register_task<SimpleTask>("simple1");
  registry_->register_task<SimpleTask>("simple2");

  auto task1 = registry_->create("simple1");
  auto task2 = registry_->create("simple2");

  ASSERT_TRUE(static_cast<bool>(task1));
  ASSERT_TRUE(static_cast<bool>(task2));
  // Same C++ type registered under two keys — type_name() matches.
  EXPECT_EQ(task1.type_name(), task2.type_name());
}

// Test task execution
TEST_F(CoreTaskRegistryTest, TaskExecution) {
  registry_->register_task<SimpleTask>("simple");

  auto task = registry_->create("simple");
  ASSERT_TRUE(static_cast<bool>(task));

  taskflow::core::task_ctx ctx;
  EXPECT_EQ(task.execute(ctx), taskflow::core::task_state::success);
}

// Test stateful task creation
TEST_F(CoreTaskRegistryTest, StatefulTaskCreation) {
  registry_->register_task<StatefulTask>("stateful");

  auto task1 = registry_->create("stateful");
  auto task2 = registry_->create("stateful");

  ASSERT_TRUE(static_cast<bool>(task1));
  ASSERT_TRUE(static_cast<bool>(task2));

  // Each creation should be a separate instance
  taskflow::core::task_ctx ctx1, ctx2;
  EXPECT_EQ(task1.execute(ctx1), taskflow::core::task_state::success);
  EXPECT_EQ(task2.execute(ctx2), taskflow::core::task_state::success);
}

// Test task with different return states
TEST_F(CoreTaskRegistryTest, DifferentReturnStates) {
  registry_->register_task<StateTask>("state");

  auto task = registry_->create("state");
  ASSERT_TRUE(static_cast<bool>(task));

  taskflow::core::task_ctx ctx;
  EXPECT_EQ(task.execute(ctx), taskflow::core::task_state::failed);
}

// Test task factory with captures
TEST_F(CoreTaskRegistryTest, TaskFactoryWithCaptures) {
  int counter = 0;
  registry_->register_task("counter", [&counter]() {
    return taskflow::core::task_wrapper{[&counter](taskflow::core::task_ctx&) mutable {
      counter++;
      return taskflow::core::task_state::success;
    }};
  });

  auto task1 = registry_->create("counter");
  auto task2 = registry_->create("counter");

  ASSERT_TRUE(static_cast<bool>(task1));
  ASSERT_TRUE(static_cast<bool>(task2));

  taskflow::core::task_ctx ctx;
  EXPECT_EQ(task1.execute(ctx), taskflow::core::task_state::success);
  EXPECT_EQ(task2.execute(ctx), taskflow::core::task_state::success);
  EXPECT_EQ(counter, 2);
}

// Test task type name
TEST_F(CoreTaskRegistryTest, TaskTypeName) {
  registry_->register_task<SimpleTask>("simple");

  auto task = registry_->create("simple");
  ASSERT_TRUE(static_cast<bool>(task));
  EXPECT_NE(std::string(task.type_name()).find("SimpleTask"), std::string::npos);
}

// Test empty registry
TEST_F(CoreTaskRegistryTest, EmptyRegistry) {
  EXPECT_FALSE(registry_->create("any").operator bool());
}

// Registry is copyable; copies are independent (separate factory maps).
TEST_F(CoreTaskRegistryTest, CopyIsIndependent) {
  registry_->register_task<SimpleTask>("simple");
  taskflow::engine::task_registry copy = *registry_;

  auto t1 = registry_->create("simple");
  auto t2 = copy.create("simple");
  ASSERT_TRUE(static_cast<bool>(t1));
  ASSERT_TRUE(static_cast<bool>(t2));

  copy.register_task<StateTask>("only_in_copy");
  EXPECT_FALSE(registry_->create("only_in_copy").operator bool());
  EXPECT_TRUE(copy.create("only_in_copy").operator bool());
}

// Test registry move semantics
TEST_F(CoreTaskRegistryTest, MoveSemantics) {
  registry_->register_task<SimpleTask>("simple");
  auto moved = std::move(*registry_);

  auto task = moved.create("simple");
  ASSERT_TRUE(static_cast<bool>(task));
  EXPECT_NE(std::string(task.type_name()).find("SimpleTask"), std::string::npos);
}

}  // namespace
