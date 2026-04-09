#include "taskflow/engine/scheduler.hpp"

#include "taskflow/engine/execution.hpp"
#include "taskflow/workflow/blueprint.hpp"

namespace taskflow::engine {

static bool incoming_edges_satisfied(const workflow_execution& execution, const workflow::workflow_blueprint& bp,
                                     std::size_t node_id) {
  for (const workflow::edge_def* ep : bp.incoming_edges(node_id)) {
    if (!ep) continue;
    const workflow::edge_def& e = *ep;
    auto ps = execution.get_node_state(e.from);

    if (ps.state == core::task_state::failed) {
      return false;
    }

    if (ps.state == core::task_state::skipped) {
      if (e.condition) {
        continue;
      }
      continue;
    }

    if (ps.state == core::task_state::success || ps.state == core::task_state::compensated) {
      if (e.condition && !(*e.condition)(execution.context())) {
        return false;
      }
      continue;
    }

    if (ps.state == core::task_state::compensation_failed) {
      return false;
    }

    return false;
  }
  return true;
}

std::vector<std::size_t> scheduler::ready_nodes(const workflow_execution& execution) {
  std::vector<std::size_t> ready;
  const auto* bp = execution.blueprint();
  if (!bp) return ready;

  for (const auto& [node_id, _] : bp->nodes()) {
    (void)_;
    auto ns = execution.get_node_state(node_id);
    if (ns.state != core::task_state::pending && ns.state != core::task_state::retry) continue;

    if (!incoming_edges_satisfied(execution, *bp, node_id)) continue;

    ready.push_back(node_id);
  }
  return ready;
}

std::size_t scheduler::pick_next(const workflow_execution& execution) {
  auto ready = ready_nodes(execution);
  return ready.empty() ? 0 : ready[0];
}

std::vector<std::size_t> scheduler::ready_nodes_ordered(const workflow_execution& execution) {
  return ready_nodes(execution);
}

bool scheduler::has_pending(const workflow_execution& execution) {
  const auto* bp = execution.blueprint();
  if (!bp) return false;
  for (const auto& [_, ns] : execution.node_states()) {
    if (ns.state == core::task_state::pending || ns.state == core::task_state::running ||
        ns.state == core::task_state::retry || ns.state == core::task_state::compensating)
      return true;
  }
  return false;
}

}  // namespace taskflow::engine
