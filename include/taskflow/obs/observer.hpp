#pragma once

#include <chrono>
#include <cstdint>
#include <string_view>

#include "taskflow/core/types.hpp"

namespace taskflow::obs {

class observer {
 public:
  virtual ~observer() = default;

  virtual void on_node_ready(std::size_t /*exec_id*/, std::size_t /*node_id*/) noexcept {}

  virtual void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                             std::int32_t attempt) noexcept = 0;

  virtual void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                                std::chrono::milliseconds duration_ms) noexcept = 0;

  virtual void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                            std::string_view error, std::chrono::milliseconds duration_ms) noexcept = 0;

  virtual void on_task_retry(std::size_t /*exec_id*/, std::size_t /*node_id*/, std::string_view /*task_type*/,
                             std::int32_t /*attempt*/, std::chrono::milliseconds /*delay_ms*/) noexcept {}

  virtual void on_workflow_complete(std::size_t exec_id, core::task_state state,
                                    std::chrono::milliseconds duration_ms) noexcept = 0;
};

}  // namespace taskflow::obs
