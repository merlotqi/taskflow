#pragma once

#include <cstdint>
#include <limits>
#include <mutex>
#include <string>
#include <unordered_map>

#include "taskflow/core/types.hpp"
#include "taskflow/observer/observer.hpp"

namespace taskflow::observer {

struct task_metrics {
  std::size_t start_count = 0;
  std::size_t success_count = 0;
  std::size_t fail_count = 0;
  std::int64_t total_duration_ms = 0;
  std::int64_t min_duration_ms = std::numeric_limits<std::int64_t>::max();
  std::int64_t max_duration_ms = 0;
};

class metrics_observer : public observer {
 public:
  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override;

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::int64_t duration_ms) noexcept override;

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type, std::string_view error,
                    std::int64_t duration_ms) noexcept override;

  void on_workflow_complete(std::size_t exec_id, core::task_state state, std::int64_t duration_ms) noexcept override;

  [[nodiscard]] const task_metrics& get_metrics(const std::string& task_type) const;
  [[nodiscard]] const std::unordered_map<std::string, task_metrics>& all_metrics() const;
  void reset();

 private:
  mutable std::mutex mutex_;
  std::unordered_map<std::string, task_metrics> metrics_;
};

}  // namespace taskflow::observer
