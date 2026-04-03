#pragma once

#include <chrono>
#include <cstdint>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <unordered_set>

#include "taskflow/core/cancellation_token.hpp"
#include "taskflow/core/result_storage.hpp"
#include "taskflow/core/task_ctx.hpp"
#include "taskflow/core/types.hpp"
#include "taskflow/engine/result_collector.hpp"
#include "taskflow/workflow/blueprint.hpp"

namespace taskflow::core {
class audit_log;
}

namespace taskflow::engine {

class workflow_execution {
 public:
  workflow_execution();
  workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp);
  workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp, core::result_storage* result_storage);
  workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp, core::result_storage* result_storage,
                     core::audit_log* audit_log);

  workflow_execution(const workflow_execution&) = delete;
  workflow_execution& operator=(const workflow_execution&) = delete;
  workflow_execution(workflow_execution&& o) noexcept;
  workflow_execution& operator=(workflow_execution&& o) noexcept;

  [[nodiscard]] std::size_t id() const noexcept;
  [[nodiscard]] const workflow::workflow_blueprint* blueprint() const noexcept;

  [[nodiscard]] core::node_state get_node_state(std::size_t node_id) const;
  void set_node_state(std::size_t node_id, core::task_state state);
  void set_node_error(std::size_t node_id, std::string error);
  void increment_retry(std::size_t node_id);
  [[nodiscard]] std::int32_t retry_count(std::size_t node_id) const;
  /// Unsynchronized view of node states; unsafe under parallel execution. Prefer get_node_state(id).
  [[nodiscard]] const std::unordered_map<std::size_t, core::node_state>& node_states() const noexcept;

  [[nodiscard]] core::task_ctx& context() noexcept;
  [[nodiscard]] const core::task_ctx& context() const noexcept;

  [[nodiscard]] std::chrono::system_clock::time_point start_time() const noexcept;
  [[nodiscard]] std::chrono::system_clock::time_point end_time() const noexcept;
  void mark_started();
  void mark_completed();

  [[nodiscard]] core::task_state overall_state() const;
  [[nodiscard]] bool is_complete() const;
  [[nodiscard]] std::size_t count_by_state(core::task_state state) const;

  [[nodiscard]] result_collector& results() noexcept;
  [[nodiscard]] const result_collector& results() const noexcept;

  [[nodiscard]] bool is_node_completed(std::size_t node_id) const;
  void mark_node_completed(std::size_t node_id);

  [[nodiscard]] std::string to_snapshot_json() const;

  // Cancellation support
  void cancel();
  [[nodiscard]] bool is_cancelled() const noexcept;
  [[nodiscard]] core::cancellation_token token() const;

 private:
  std::size_t exec_id_ = 0;
  core::cancellation_source cancel_source_;
  std::unique_ptr<workflow::workflow_blueprint> blueprint_;
  std::unordered_map<std::size_t, core::node_state> node_states_;
  std::unordered_set<core::idempotency_key, core::idempotency_key_hash> completed_nodes_;
  core::task_ctx ctx_;
  result_collector results_;
  std::chrono::system_clock::time_point start_time_{};
  std::chrono::system_clock::time_point end_time_{};
  core::audit_log* audit_log_ = nullptr;

  mutable std::unique_ptr<std::mutex> state_mutex_;

  void init_node_states();
};

}  // namespace taskflow::engine
