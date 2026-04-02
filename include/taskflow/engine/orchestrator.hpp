#pragma once

#include <taskflow/core/types.hpp>
#include <taskflow/engine/execution.hpp>
#include <taskflow/engine/executor.hpp>
#include <taskflow/engine/registry.hpp>
#include <taskflow/engine/scheduler.hpp>
#include <taskflow/observer/observer.hpp>
#include <taskflow/storage/storage.hpp>
#include <taskflow/workflow/blueprint.hpp>

#include <memory>
#include <unordered_map>
#include <vector>

namespace tf {

class Orchestrator {
 public:
  void register_task_type(std::string type, TaskFactoryFn factory);

  void set_storage(std::shared_ptr<StateStorage> storage);

  void add_observer(std::shared_ptr<Observer> observer);

  ExecutionId create_execution(const WorkflowBlueprint& blueprint);

  void run_sync(const ExecutionId& id);

  const WorkflowExecution* get_execution(const ExecutionId& id) const;
  WorkflowExecution* get_execution(const ExecutionId& id);

 private:
  TaskRegistry registry_;
  Scheduler scheduler_;
  Executor executor_;
  std::unordered_map<ExecutionId, WorkflowExecution> executions_;
  std::shared_ptr<StateStorage> storage_;
  std::vector<std::shared_ptr<Observer>> observers_;
  std::uint64_t next_exec_{0};
};

}  // namespace tf
