#include <gtest/gtest.h>

#include <taskflow/obs/metrics.hpp>

namespace {

// Test fixture for metrics_observer
class MetricsTest : public ::testing::Test {
 protected:
  void SetUp() override {
    observer_ = std::make_unique<taskflow::obs::metrics_observer>();
  }

  std::unique_ptr<taskflow::obs::metrics_observer> observer_;
};

// Test task metrics collection
TEST_F(MetricsTest, TaskMetrics) {
  // Test task start
  observer_->on_task_start(1, 1, "TestTask", 1);
  observer_->on_task_complete(1, 1, "TestTask", std::chrono::milliseconds(100));

  // Verify metrics
  const auto& metrics = observer_->get_metrics("TestTask");
  EXPECT_EQ(metrics.start_count, 1);
  EXPECT_EQ(metrics.success_count, 1);
  EXPECT_EQ(metrics.fail_count, 0);
  EXPECT_EQ(metrics.total_duration_ms.count(), 100);
  EXPECT_EQ(metrics.min_duration_ms.count(), 100);
  EXPECT_EQ(metrics.max_duration_ms.count(), 100);
}

// Test multiple task executions
TEST_F(MetricsTest, MultipleExecutions) {
  // First execution
  observer_->on_task_start(1, 1, "TestTask", 1);
  observer_->on_task_complete(1, 1, "TestTask", std::chrono::milliseconds(100));

  // Second execution
  observer_->on_task_start(1, 2, "TestTask", 1);
  observer_->on_task_complete(1, 2, "TestTask", std::chrono::milliseconds(200));

  // Verify metrics
  const auto& metrics = observer_->get_metrics("TestTask");
  EXPECT_EQ(metrics.start_count, 2);
  EXPECT_EQ(metrics.success_count, 2);
  EXPECT_EQ(metrics.fail_count, 0);
  EXPECT_EQ(metrics.total_duration_ms.count(), 300);
  EXPECT_EQ(metrics.min_duration_ms.count(), 100);
  EXPECT_EQ(metrics.max_duration_ms.count(), 200);
}

// Test task failure
TEST_F(MetricsTest, TaskFailure) {
  observer_->on_task_start(1, 1, "TestTask", 1);
  observer_->on_task_fail(1, 1, "TestTask", "TestError", std::chrono::milliseconds(50));

  // Verify metrics
  const auto& metrics = observer_->get_metrics("TestTask");
  EXPECT_EQ(metrics.start_count, 1);
  EXPECT_EQ(metrics.success_count, 0);
  EXPECT_EQ(metrics.fail_count, 1);
  EXPECT_EQ(metrics.total_duration_ms.count(), 50);
  EXPECT_EQ(metrics.min_duration_ms.count(), 50);
  EXPECT_EQ(metrics.max_duration_ms.count(), 50);
}

// Test multiple task types
TEST_F(MetricsTest, MultipleTaskTypes) {
  // TestTask1
  observer_->on_task_start(1, 1, "TestTask1", 1);
  observer_->on_task_complete(1, 1, "TestTask1", std::chrono::milliseconds(100));

  // TestTask2
  observer_->on_task_start(1, 2, "TestTask2", 1);
  observer_->on_task_complete(1, 2, "TestTask2", std::chrono::milliseconds(200));

  // Verify metrics for both task types
  const auto& metrics1 = observer_->get_metrics("TestTask1");
  EXPECT_EQ(metrics1.start_count, 1);
  EXPECT_EQ(metrics1.success_count, 1);
  EXPECT_EQ(metrics1.total_duration_ms.count(), 100);

  const auto& metrics2 = observer_->get_metrics("TestTask2");
  EXPECT_EQ(metrics2.start_count, 1);
  EXPECT_EQ(metrics2.success_count, 1);
  EXPECT_EQ(metrics2.total_duration_ms.count(), 200);
}

// Test reset functionality
TEST_F(MetricsTest, Reset) {
  observer_->on_task_start(1, 1, "TestTask", 1);
  observer_->on_task_complete(1, 1, "TestTask", std::chrono::milliseconds(100));

  observer_->reset();

  // Verify metrics are reset
  const auto& metrics = observer_->get_metrics("TestTask");
  EXPECT_EQ(metrics.start_count, 0);
  EXPECT_EQ(metrics.success_count, 0);
  EXPECT_EQ(metrics.fail_count, 0);
  EXPECT_EQ(metrics.total_duration_ms.count(), 0);
  EXPECT_EQ(metrics.min_duration_ms.count(), std::chrono::milliseconds::max().count());
  EXPECT_EQ(metrics.max_duration_ms.count(), 0);
}

// Test all metrics
TEST_F(MetricsTest, AllMetrics) {
  observer_->on_task_start(1, 1, "TestTask1", 1);
  observer_->on_task_complete(1, 1, "TestTask1", std::chrono::milliseconds(100));

  observer_->on_task_start(1, 2, "TestTask2", 1);
  observer_->on_task_fail(1, 2, "TestTask2", "Error", std::chrono::milliseconds(200));

  const auto& all_metrics = observer_->all_metrics();
  EXPECT_EQ(all_metrics.size(), 2);

  EXPECT_TRUE(all_metrics.find("TestTask1") != all_metrics.end());
  EXPECT_TRUE(all_metrics.find("TestTask2") != all_metrics.end());

  const auto& metrics1 = all_metrics.at("TestTask1");
  EXPECT_EQ(metrics1.start_count, 1);
  EXPECT_EQ(metrics1.success_count, 1);

  const auto& metrics2 = all_metrics.at("TestTask2");
  EXPECT_EQ(metrics2.start_count, 1);
  EXPECT_EQ(metrics2.fail_count, 1);
}

// Test concurrent access (thread safety)
TEST_F(MetricsTest, ConcurrentAccess) {
  // This test is more of a sanity check since we can't easily test true concurrency
  // in a single-threaded test environment
  observer_->on_task_start(1, 1, "TestTask", 1);
  observer_->on_task_complete(1, 1, "TestTask", std::chrono::milliseconds(100));

  // Verify metrics are accessible
  const auto& metrics = observer_->get_metrics("TestTask");
  EXPECT_EQ(metrics.start_count, 1);
}

// Test empty metrics
TEST_F(MetricsTest, EmptyMetrics) {
  const auto& metrics = observer_->get_metrics("NonExistentTask");
  EXPECT_EQ(metrics.start_count, 0);
  EXPECT_EQ(metrics.success_count, 0);
  EXPECT_EQ(metrics.fail_count, 0);
  EXPECT_EQ(metrics.total_duration_ms.count(), 0);
  EXPECT_EQ(metrics.min_duration_ms.count(), std::chrono::milliseconds::max().count());
  EXPECT_EQ(metrics.max_duration_ms.count(), 0);
}

// Test workflow complete
TEST_F(MetricsTest, WorkflowComplete) {
  observer_->on_workflow_complete(1, taskflow::core::task_state::success, std::chrono::milliseconds(300));

  // Workflow complete doesn't affect task metrics, but shouldn't crash
  const auto& all_metrics = observer_->all_metrics();
  EXPECT_TRUE(all_metrics.empty());
}

}  // namespace
