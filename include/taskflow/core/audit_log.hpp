#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include <taskflow/core/types.hpp>

namespace taskflow::core {

struct audit_entry {
  std::size_t exec_id = 0;
  std::size_t node_id = 0;
  task_state old_state = task_state::pending;
  task_state new_state = task_state::pending;
  std::int64_t timestamp = 0;
  std::string error_message;
};

class audit_log {
 public:
  void record(std::size_t exec_id, std::size_t node_id, task_state old_state, task_state new_state,
              const std::string& error = "");

  [[nodiscard]] std::vector<audit_entry> get_history(std::size_t exec_id) const;
  [[nodiscard]] std::vector<audit_entry> get_node_history(std::size_t exec_id, std::size_t node_id) const;
  [[nodiscard]] const std::vector<audit_entry>& all_entries() const;

  void clear();

 private:
  std::vector<audit_entry> entries_;
};

}  // namespace taskflow::core
