#include <taskflow/engine/execution.hpp>

namespace tf {

void WorkflowExecution::init_states() {
  node_states.clear();
  retry_count.clear();
  for (const auto& n : blueprint.nodes()) {
    node_states[n.id] = TaskState::Pending;
    retry_count[n.id] = 0;
  }
}

bool WorkflowExecution::all_finished() const {
  for (const auto& n : blueprint.nodes()) {
    TaskState s = state_of(n.id);
    if (s != TaskState::Success && s != TaskState::Failed) {
      return false;
    }
  }
  return true;
}

bool WorkflowExecution::any_failed() const {
  for (const auto& n : blueprint.nodes()) {
    if (state_of(n.id) == TaskState::Failed) {
      return true;
    }
  }
  return false;
}

TaskState WorkflowExecution::state_of(const NodeId& id) const {
  auto it = node_states.find(id);
  if (it == node_states.end()) {
    return TaskState::Pending;
  }
  return it->second;
}

}  // namespace tf
