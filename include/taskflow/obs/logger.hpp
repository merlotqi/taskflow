#pragma once

#include <iostream>
#include <string_view>

#include "taskflow/obs/observer.hpp"

namespace taskflow::obs {

class logging_observer : public observer {
 public:
  explicit logging_observer(std::ostream& os = std::cout);

  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t attempt) noexcept override;

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds duration_ms) noexcept override;

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type, std::string_view error,
                    std::chrono::milliseconds duration_ms) noexcept override;

  void on_compensation_start(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                             std::int32_t attempt) noexcept override;

  void on_compensation_complete(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                                std::chrono::milliseconds duration_ms) noexcept override;

  void on_compensation_fail(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                            std::string_view error, std::chrono::milliseconds duration_ms) noexcept override;

  void on_workflow_complete(std::size_t exec_id, core::task_state state,
                            std::chrono::milliseconds duration_ms) noexcept override;

 private:
  std::ostream& os_;
};

}  // namespace taskflow::obs
