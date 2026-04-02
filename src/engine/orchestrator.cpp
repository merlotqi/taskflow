#include <taskflow/engine/orchestrator.hpp>
#include <taskflow/workflow/serializer.hpp>

namespace tf {

void Orchestrator::register_task_type(std::string type, TaskFactoryFn factory) {
  registry_.register_type(std::move(type), std::move(factory));
}

void Orchestrator::set_storage(std::shared_ptr<StateStorage> storage) {
  storage_ = std::move(storage);
}

void Orchestrator::add_observer(std::shared_ptr<Observer> observer) {
  if (observer) {
    observers_.push_back(std::move(observer));
  }
}

ExecutionId Orchestrator::create_execution(const WorkflowBlueprint& blueprint) {
  WorkflowBlueprint copy = blueprint;
  copy.validate_acyclic();

  WorkflowExecution ex;
  ex.id = "exec_" + std::to_string(++next_exec_);
  ex.blueprint = std::move(copy);
  ex.init_states();

  ExecutionId id = ex.id;
  executions_.insert_or_assign(id, std::move(ex));

  if (storage_) {
    storage_->save_execution(*get_execution(id));
  }
  return id;
}

void Orchestrator::run_sync(const ExecutionId& id) {
  WorkflowExecution* ex = get_execution(id);
  if (!ex) {
    throw std::runtime_error("unknown execution: " + id);
  }

  while (!ex->all_finished()) {
    if (ex->any_failed()) {
      break;
    }
    std::vector<NodeId> ready = scheduler_.ready_nodes(*ex);
    if (ready.empty()) {
      break;
    }
    for (const NodeId& nid : ready) {
      executor_.run_node(*ex, nid, registry_, observers_);
      if (storage_) {
        storage_->save_execution(*ex);
      }
      if (ex->state_of(nid) == TaskState::Failed) {
        break;
      }
    }
  }
}

const WorkflowExecution* Orchestrator::get_execution(const ExecutionId& id) const {
  auto it = executions_.find(id);
  if (it == executions_.end()) {
    return nullptr;
  }
  return &it->second;
}

WorkflowExecution* Orchestrator::get_execution(const ExecutionId& id) {
  auto it = executions_.find(id);
  if (it == executions_.end()) {
    return nullptr;
  }
  return &it->second;
}

}  // namespace tf
