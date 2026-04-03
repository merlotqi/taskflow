#include "taskflow/observer/metrics.hpp"

#include <algorithm>
#include <string>

namespace taskflow::observer {

void metrics_observer::on_task_start(std::size_t /*exec_id*/, std::size_t /*node_id*/,
                                     std::string_view task_type, std::int32_t /*attempt*/) noexcept {
  std::lock_guard lock(mutex_);
  metrics_[std::string(task_type)].start_count++;
}

void metrics_observer::on_task_complete(std::size_t /*exec_id*/, std::size_t /*node_id*/,
                                        std::string_view task_type, std::int64_t duration_ms) noexcept {
  std::lock_guard lock(mutex_);
  auto& m = metrics_[std::string(task_type)];
  m.success_count++;
  m.total_duration_ms += duration_ms;
  m.min_duration_ms = std::min(m.min_duration_ms, duration_ms);
  m.max_duration_ms = std::max(m.max_duration_ms, duration_ms);
}

void metrics_observer::on_task_fail(std::size_t /*exec_id*/, std::size_t /*node_id*/, std::string_view task_type,
                                    std::string_view /*error*/, std::int64_t duration_ms) noexcept {
  std::lock_guard lock(mutex_);
  auto& m = metrics_[std::string(task_type)];
  m.fail_count++;
  m.total_duration_ms += duration_ms;
  m.min_duration_ms = std::min(m.min_duration_ms, duration_ms);
  m.max_duration_ms = std::max(m.max_duration_ms, duration_ms);
}

void metrics_observer::on_workflow_complete(std::size_t /*exec_id*/, core::task_state /*state*/,
                                            std::int64_t /*duration_ms*/) noexcept {
  // Workflow-level metrics can be added here
}

const task_metrics& metrics_observer::get_metrics(const std::string& task_type) const {
  std::lock_guard lock(mutex_);
  static const task_metrics empty{};
  auto it = metrics_.find(task_type);
  return it != metrics_.end() ? it->second : empty;
}

const std::unordered_map<std::string, task_metrics>& metrics_observer::all_metrics() const {
  std::lock_guard lock(mutex_);
  return metrics_;
}

void metrics_observer::reset() {
  std::lock_guard lock(mutex_);
  metrics_.clear();
}

}  // namespace taskflow::observer
