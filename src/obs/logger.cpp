#include "taskflow/obs/logger.hpp"

#include <chrono>

#include "taskflow/core/types.hpp"

namespace taskflow::obs {

logging_observer::logging_observer(std::ostream& os) : os_(os) {}

void logging_observer::on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                                     std::int32_t attempt) noexcept {
  os_ << "[START] exec=" << exec_id << " node=" << node_id << " type=" << task_type << " attempt=" << attempt << "\n";
}

void logging_observer::on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                                        std::chrono::milliseconds duration_ms) noexcept {
  os_ << "[DONE]  exec=" << exec_id << " node=" << node_id << " type=" << task_type
      << " duration=" << duration_ms.count() << "ms\n";
}

void logging_observer::on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                                    std::string_view error, std::chrono::milliseconds duration_ms) noexcept {
  os_ << "[FAIL]  exec=" << exec_id << " node=" << node_id << " type=" << task_type << " error=" << error
      << " duration=" << duration_ms.count() << "ms\n";
}

void logging_observer::on_compensation_start(std::size_t exec_id, std::size_t node_id,
                                             std::string_view compensate_task_type, std::int32_t attempt) noexcept {
  os_ << "[COMP-START] exec=" << exec_id << " node=" << node_id << " comp_type=" << compensate_task_type
      << " attempt=" << attempt << "\n";
}

void logging_observer::on_compensation_complete(std::size_t exec_id, std::size_t node_id,
                                                std::string_view compensate_task_type,
                                                std::chrono::milliseconds duration_ms) noexcept {
  os_ << "[COMP-OK]   exec=" << exec_id << " node=" << node_id << " comp_type=" << compensate_task_type
      << " duration=" << duration_ms.count() << "ms\n";
}

void logging_observer::on_compensation_fail(std::size_t exec_id, std::size_t node_id,
                                            std::string_view compensate_task_type, std::string_view error,
                                            std::chrono::milliseconds duration_ms) noexcept {
  os_ << "[COMP-FAIL] exec=" << exec_id << " node=" << node_id << " comp_type=" << compensate_task_type
      << " error=" << error << " duration=" << duration_ms.count() << "ms\n";
}

void logging_observer::on_workflow_complete(std::size_t exec_id, core::task_state state,
                                            std::chrono::milliseconds duration_ms) noexcept {
  os_ << "[WORKFLOW] exec=" << exec_id << " state=" << core::to_string(state) << " duration=" << duration_ms.count()
      << "ms\n";
}

}  // namespace taskflow::obs
