#pragma once

#include <cstdint>
#include <string_view>

#include "taskflow/core/types.hpp"

namespace taskflow::observer {

class observer {
 public:
  virtual ~observer() = default;

  /// Called when a node becomes ready to run (before submit). Default: no-op.
  virtual void on_node_ready(std::size_t /*exec_id*/, std::size_t /*node_id*/) noexcept {}

  virtual void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                             std::int32_t attempt) noexcept = 0;

  virtual void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                                std::int64_t duration_ms) noexcept = 0;

  virtual void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                            std::string_view error, std::int64_t duration_ms) noexcept = 0;

  virtual void on_workflow_complete(std::size_t exec_id, core::task_state state, std::int64_t duration_ms) noexcept = 0;
};

}  // namespace taskflow::observer
