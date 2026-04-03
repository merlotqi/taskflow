#pragma once

#include <cstddef>
#include <functional>

namespace taskflow::integration {

struct workflow_event_hooks {
  std::function<void(std::size_t exec_id, std::size_t node_id)> on_node_ready;
  std::function<void(std::size_t exec_id, std::size_t node_id)> on_node_started;
  std::function<void(std::size_t exec_id, std::size_t node_id, bool success)> on_node_finished;
  std::function<void(std::size_t exec_id, bool success)> on_workflow_finished;
};

}  // namespace taskflow::integration
