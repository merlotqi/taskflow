#include <gtest/gtest.h>

#include <sstream>
#include <string>

#include <taskflow/obs/logger.hpp>

namespace {

TEST(LoggerObserverTest, WritesToStreamWithoutThrowing) {
  std::ostringstream oss;
  taskflow::obs::logging_observer log{oss};
  EXPECT_NO_THROW(log.on_task_start(1, 2, "t", 1));
  EXPECT_NO_THROW(log.on_task_complete(1, 2, "t", std::chrono::milliseconds{5}));
  EXPECT_NO_THROW(log.on_task_fail(1, 2, "t", "err", std::chrono::milliseconds{3}));
  EXPECT_NO_THROW(log.on_workflow_complete(1, taskflow::core::task_state::success, std::chrono::milliseconds{10}));
  std::string s = oss.str();
  EXPECT_FALSE(s.empty());
}

}  // namespace
