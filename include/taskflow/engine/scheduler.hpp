#pragma once

#include <taskflow/core/types.hpp>
#include <taskflow/engine/execution.hpp>

#include <vector>

namespace tf {

class Scheduler {
 public:
  std::vector<NodeId> ready_nodes(const WorkflowExecution& ex) const;
};

}  // namespace tf
