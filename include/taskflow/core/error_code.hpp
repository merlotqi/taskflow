#pragma once

#include <cstdint>
#include <string>
#include <system_error>

namespace taskflow::core {

// Error codes
enum class errc : std::uint8_t {
  success = 0,
  task_execution_failed = 1,
  task_timeout = 2,
  task_cancelled = 3,
  blueprint_not_found = 4,
  execution_not_found = 5,
  invalid_blueprint = 6,
  task_type_not_found = 7,
  storage_error = 8,
  node_already_running = 9,
  invalid_state_transition = 10,
  max_retry_exceeded = 11,
  dependency_failed = 12,
  operation_not_permitted = 13,
};

// Error category
class taskflow_category : public std::error_category {
 public:
  const char* name() const noexcept override { return "taskflow"; }

  std::string message(int ev) const override {
    switch (static_cast<errc>(ev)) {
      case errc::success:
        return "success";
      case errc::task_execution_failed:
        return "task execution failed";
      case errc::task_timeout:
        return "task timeout";
      case errc::task_cancelled:
        return "task cancelled";
      case errc::blueprint_not_found:
        return "blueprint not found";
      case errc::execution_not_found:
        return "execution not found";
      case errc::invalid_blueprint:
        return "invalid blueprint";
      case errc::task_type_not_found:
        return "task type not found";
      case errc::storage_error:
        return "storage error";
      case errc::node_already_running:
        return "node is already running";
      case errc::invalid_state_transition:
        return "invalid state transition";
      case errc::max_retry_exceeded:
        return "maximum retry attempts exceeded";
      case errc::dependency_failed:
        return "dependency node failed";
      case errc::operation_not_permitted:
        return "operation not permitted";
      default:
        return "unknown error";
    }
  }
};

// Get error category instance
inline const std::error_category& taskflow_category() {
  static class taskflow_category instance;
  return instance;
}

// Make error code
inline std::error_code make_error_code(errc e) { return {static_cast<int>(e), taskflow_category()}; }

}  // namespace taskflow::core

// Enable implicit conversion to error_code
namespace std {
template <>
struct is_error_code_enum<taskflow::core::errc> : true_type {};
}  // namespace std
