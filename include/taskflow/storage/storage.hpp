#pragma once

#include <taskflow/engine/execution.hpp>

#include <string>

namespace tf {

class StateStorage {
 public:
  virtual ~StateStorage() = default;

  virtual void save_execution(const WorkflowExecution& ex) = 0;

  virtual bool load_execution(const ExecutionId& id, WorkflowExecution& out) = 0;
};

}  // namespace tf
