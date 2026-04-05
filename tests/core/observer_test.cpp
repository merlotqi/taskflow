#include <gtest/gtest.h>

#include <taskflow/obs/observer.hpp>

namespace {

// Test fixture for observer
class ObserverTest : public ::testing::Test {
 protected:
  void SetUp() override {
    // Setup if needed
  }
};

// Mock observer implementation
class MockObserver : public taskflow::obs::observer {
 public:
  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override {
    task_start_calls_++;
    last_exec_id_ = exec_id;
    last_node_id_ = node_id;
    last_task_type_ = task_type;
    last_attempt_ = attempt;
  }

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds duration_ms) noexcept override {
    task_complete_calls_++;
    last_exec_id_ = exec_id;
    last_node_id_ = node_id;
    last_task_type_ = task_type;
    last_duration_ = duration_ms;
  }

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                    std::string_view error, std::chrono::milliseconds duration_ms) noexcept override {
    task_fail_calls_++;
    last_exec_id_ = exec_id;
    last_node_id_ = node_id;
    last_task_type_ = task_type;
    last_error_ = error;
    last_duration_ = duration_ms;
  }

  void on_workflow_complete(std::size_t exec_id, taskflow::core::task_state state,
                            std::chrono::milliseconds duration_ms) noexcept override {
    workflow_complete_calls_++;
    last_exec_id_ = exec_id;
    last_state_ = state;
    last_duration_ = duration_ms;
  }

  // Optional callbacks
  void on_node_ready(std::size_t exec_id, std::size_t node_id) noexcept override {
    node_ready_calls_++;
    last_exec_id_ = exec_id;
    last_node_id_ = node_id;
  }

  void on_task_retry(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt, std::chrono::milliseconds delay_ms) noexcept override {
    task_retry_calls_++;
    last_exec_id_ = exec_id;
    last_node_id_ = node_id;
    last_task_type_ = task_type;
    last_attempt_ = attempt;
    last_delay_ = delay_ms;
  }

  // Call counters
  int task_start_calls_ = 0;
  int task_complete_calls_ = 0;
  int task_fail_calls_ = 0;
  int workflow_complete_calls_ = 0;
  int node_ready_calls_ = 0;
  int task_retry_calls_ = 0;

  // Last values
  std::size_t last_exec_id_ = 0;
  std::size_t last_node_id_ = 0;
  std::string_view last_task_type_;
  std::int32_t last_attempt_ = 0;
  std::chrono::milliseconds last_duration_{0};
  std::string_view last_error_;
  taskflow::core::task_state last_state_ = taskflow::core::task_state::pending;
  std::chrono::milliseconds last_delay_{0};
};

// Test required callbacks
TEST_F(ObserverTest, RequiredCallbacks) {
  MockObserver observer;

  // Test task start
  observer.on_task_start(1, 2, "TestTask", 1);
  EXPECT_EQ(observer.task_start_calls_, 1);
  EXPECT_EQ(observer.last_exec_id_, 1);
  EXPECT_EQ(observer.last_node_id_, 2);
  EXPECT_EQ(observer.last_task_type_, "TestTask");
  EXPECT_EQ(observer.last_attempt_, 1);

  // Test task complete
  observer.on_task_complete(1, 2, "TestTask", std::chrono::milliseconds(100));
  EXPECT_EQ(observer.task_complete_calls_, 1);
  EXPECT_EQ(observer.last_duration_.count(), 100);

  // Test task fail
  observer.on_task_fail(1, 2, "TestTask", "TestError", std::chrono::milliseconds(50));
  EXPECT_EQ(observer.task_fail_calls_, 1);
  EXPECT_EQ(observer.last_error_, "TestError");
  EXPECT_EQ(observer.last_duration_.count(), 50);

  // Test workflow complete
  observer.on_workflow_complete(1, taskflow::core::task_state::success, std::chrono::milliseconds(200));
  EXPECT_EQ(observer.workflow_complete_calls_, 1);
  EXPECT_EQ(observer.last_state_, taskflow::core::task_state::success);
  EXPECT_EQ(observer.last_duration_.count(), 200);
}

// Test optional callbacks
TEST_F(ObserverTest, OptionalCallbacks) {
  MockObserver observer;

  // Test node ready
  observer.on_node_ready(3, 4);
  EXPECT_EQ(observer.node_ready_calls_, 1);
  EXPECT_EQ(observer.last_exec_id_, 3);
  EXPECT_EQ(observer.last_node_id_, 4);

  // Test task retry
  observer.on_task_retry(5, 6, "RetryTask", 2, std::chrono::milliseconds(100));
  EXPECT_EQ(observer.task_retry_calls_, 1);
  EXPECT_EQ(observer.last_attempt_, 2);
  EXPECT_EQ(observer.last_delay_.count(), 100);
}

// Test observer lifecycle
TEST_F(ObserverTest, ObserverLifecycle) {
  MockObserver observer;

  // Verify initial state
  EXPECT_EQ(observer.task_start_calls_, 0);
  EXPECT_EQ(observer.task_complete_calls_, 0);
  EXPECT_EQ(observer.task_fail_calls_, 0);
  EXPECT_EQ(observer.workflow_complete_calls_, 0);
  EXPECT_EQ(observer.node_ready_calls_, 0);
  EXPECT_EQ(observer.task_retry_calls_, 0);
}

// Test multiple calls
TEST_F(ObserverTest, MultipleCalls) {
  MockObserver observer;

  // Multiple task starts
  observer.on_task_start(1, 1, "Task1", 1);
  observer.on_task_start(1, 2, "Task2", 1);
  EXPECT_EQ(observer.task_start_calls_, 2);
  EXPECT_EQ(observer.last_exec_id_, 1);
  EXPECT_EQ(observer.last_node_id_, 2);
  EXPECT_EQ(observer.last_task_type_, "Task2");

  // Multiple task completes
  observer.on_task_complete(1, 1, "Task1", std::chrono::milliseconds(100));
  observer.on_task_complete(1, 2, "Task2", std::chrono::milliseconds(200));
  EXPECT_EQ(observer.task_complete_calls_, 2);
  EXPECT_EQ(observer.last_duration_.count(), 200);
}

// Test different execution IDs
TEST_F(ObserverTest, DifferentExecutions) {
  MockObserver observer;

  // Different executions
  observer.on_task_start(1, 1, "Task1", 1);
  observer.on_task_start(2, 1, "Task1", 1);

  EXPECT_EQ(observer.task_start_calls_, 2);
  EXPECT_EQ(observer.last_exec_id_, 2);
  EXPECT_EQ(observer.last_node_id_, 1);
}

// Test error handling in callbacks
TEST_F(ObserverTest, ErrorHandling) {
  MockObserver observer;

  // Callbacks should not throw exceptions
  EXPECT_NO_THROW(observer.on_task_start(1, 1, "Task1", 1));
  EXPECT_NO_THROW(observer.on_task_complete(1, 1, "Task1", std::chrono::milliseconds(100)));
  EXPECT_NO_THROW(observer.on_task_fail(1, 1, "Task1", "Error", std::chrono::milliseconds(100)));
  EXPECT_NO_THROW(observer.on_workflow_complete(1, taskflow::core::task_state::success, std::chrono::milliseconds(100)));
  EXPECT_NO_THROW(observer.on_node_ready(1, 1));
  EXPECT_NO_THROW(observer.on_task_retry(1, 1, "Task1", 1, std::chrono::milliseconds(100)));
}

}  // namespace
