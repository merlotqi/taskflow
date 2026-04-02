#pragma once

#include <taskflow/core/types.hpp>
#include <taskflow/engine/execution.hpp>
#include <taskflow/engine/registry.hpp>
#include <taskflow/observer/observer.hpp>

#include <memory>
#include <vector>

namespace tf {

class Executor {
 public:
  void run_node(WorkflowExecution& ex, const NodeId& id, const TaskRegistry& registry,
                const std::vector<std::shared_ptr<Observer>>& observers);
};

}  // namespace tf
