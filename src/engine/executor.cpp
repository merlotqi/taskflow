#include <taskflow/engine/executor.hpp>

#include <exception>

namespace tf {

void Executor::run_node(WorkflowExecution& ex, const NodeId& id, const TaskRegistry& registry,
                        const std::vector<std::shared_ptr<Observer>>& observers) {
  const NodeDef* node = ex.blueprint.find_node(id);
  if (!node) {
    throw std::runtime_error("unknown node in execution: " + id);
  }

  for (const auto& o : observers) {
    if (o) {
      o->on_task_start(ex.id, id);
    }
  }

  ex.node_states[id] = TaskState::Running;

  try {
    TaskPtr task = registry.create(node->task_type);
    task->execute(ex.context);
    if (ex.context.is_cancelled()) {
      ex.node_states[id] = TaskState::Failed;
      for (const auto& o : observers) {
        if (o) {
          o->on_task_fail(ex.id, id, "cancelled");
        }
      }
      return;
    }
    ex.node_states[id] = TaskState::Success;
    for (const auto& o : observers) {
      if (o) {
        o->on_task_complete(ex.id, id);
      }
    }
  } catch (const std::exception& e) {
    ex.node_states[id] = TaskState::Failed;
    for (const auto& o : observers) {
      if (o) {
        o->on_task_fail(ex.id, id, e.what());
      }
    }
  } catch (...) {
    ex.node_states[id] = TaskState::Failed;
    for (const auto& o : observers) {
      if (o) {
        o->on_task_fail(ex.id, id, "non-std exception");
      }
    }
  }
}

}  // namespace tf
