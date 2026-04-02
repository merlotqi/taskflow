#pragma once

#include <taskflow/core/task_ctx.hpp>
#include <taskflow/core/types.hpp>
#include <taskflow/workflow/blueprint.hpp>

#include <unordered_map>

namespace tf {

struct WorkflowExecution {
  ExecutionId id;
  WorkflowBlueprint blueprint;
  std::unordered_map<NodeId, TaskState> node_states;
  TaskCtx context;
  std::unordered_map<NodeId, int> retry_count;

  void init_states();
  bool all_finished() const;
  bool any_failed() const;
  TaskState state_of(const NodeId& id) const;
};

}  // namespace tf
