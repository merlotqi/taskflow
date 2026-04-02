#include <taskflow/engine/scheduler.hpp>

#include <unordered_map>
#include <unordered_set>

namespace tf {

std::vector<NodeId> Scheduler::ready_nodes(const WorkflowExecution& ex) const {
  std::unordered_map<NodeId, std::unordered_set<NodeId>> preds;
  for (const auto& e : ex.blueprint.edges()) {
    preds[e.to].insert(e.from);
  }

  std::vector<NodeId> ready;
  for (const auto& n : ex.blueprint.nodes()) {
    if (ex.state_of(n.id) != TaskState::Pending) {
      continue;
    }
    bool ok = true;
    auto it = preds.find(n.id);
    if (it != preds.end()) {
      for (const NodeId& p : it->second) {
        if (ex.state_of(p) != TaskState::Success) {
          ok = false;
          break;
        }
      }
    }
    if (ok) {
      ready.push_back(n.id);
    }
  }
  return ready;
}

}  // namespace tf
