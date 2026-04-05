#include "taskflow/core/audit_log.hpp"

#include <algorithm>
#include <chrono>

namespace taskflow::core {

void audit_log::record(std::size_t exec_id, std::size_t node_id, task_state old_state, task_state new_state,
                       const std::string& error) {
  audit_entry entry;
  entry.exec_id = exec_id;
  entry.node_id = node_id;
  entry.old_state = old_state;
  entry.new_state = new_state;
  entry.timestamp = std::chrono::system_clock::now();
  entry.error_message = error;
  entries_.push_back(std::move(entry));
}

std::vector<audit_entry> audit_log::get_history(std::size_t exec_id) const {
  std::vector<audit_entry> result;
  std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
               [exec_id](const audit_entry& e) { return e.exec_id == exec_id; });
  return result;
}

std::vector<audit_entry> audit_log::get_node_history(std::size_t exec_id, std::size_t node_id) const {
  std::vector<audit_entry> result;
  std::copy_if(entries_.begin(), entries_.end(), std::back_inserter(result),
               [exec_id, node_id](const audit_entry& e) { return e.exec_id == exec_id && e.node_id == node_id; });
  return result;
}

const std::vector<audit_entry>& audit_log::all_entries() const { return entries_; }

void audit_log::clear() { entries_.clear(); }

}  // namespace taskflow::core
