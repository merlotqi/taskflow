#include "taskflow/engine/executor.hpp"

#include <chrono>
#include <cmath>
#include <thread>

#include "taskflow/engine/execution.hpp"
#include "taskflow/engine/registry.hpp"
#include "taskflow/observer/observer.hpp"

namespace taskflow::engine {

static std::int64_t now_ms() {
  return std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
      .count();
}

core::task_state executor::execute_node(workflow_execution& execution, std::size_t node_id,
                                        const itask_registry& registry,
                                        const std::vector<observer::observer*>& observers) {
  if (execution.is_cancelled()) {
    execution.set_node_state(node_id, core::task_state::cancelled);
    return core::task_state::cancelled;
  }

  if (execution.is_node_completed(node_id)) {
    return core::task_state::success;
  }

  const auto* bp = execution.blueprint();
  if (!bp) return core::task_state::failed;
  const auto* node = bp->find_node(node_id);
  if (!node) return core::task_state::failed;

  auto task = registry.create(node->task_type);
  if (!task) {
    execution.set_node_state(node_id, core::task_state::failed);
    execution.set_node_error(node_id, "unknown task type: " + node->task_type);
    return core::task_state::failed;
  }

  auto start = now_ms();
  for (auto* obs : observers)
    if (obs) obs->on_task_start(execution.id(), node_id, node->task_type, execution.retry_count(node_id) + 1);

  auto& ctx = execution.context();
  ctx.set_node_id(node_id);
  ctx.set_exec_id(execution.id());
  if (ctx.exec_start_time() == 0) ctx.set_exec_start_time(start);

  execution.set_node_state(node_id, core::task_state::running);
  core::task_state result = core::task_state::failed;
  try {
    result = task.execute(ctx);
  } catch (const std::exception& e) {
    execution.set_node_error(node_id, e.what());
  }

  auto end = now_ms();
  auto dur = end - start;
  if (result == core::task_state::success) {
    execution.set_node_state(node_id, core::task_state::success);
    execution.mark_node_completed(node_id);
    for (auto* obs : observers)
      if (obs) obs->on_task_complete(execution.id(), node_id, node->task_type, dur);
  } else {
    execution.set_node_state(node_id, core::task_state::failed);
    for (auto* obs : observers)
      if (obs)
        obs->on_task_fail(execution.id(), node_id, node->task_type, execution.get_node_state(node_id).error_message,
                          dur);
  }
  return result;
}

core::task_state executor::execute_with_retry(workflow_execution& execution, std::size_t node_id,
                                              const itask_registry& registry,
                                              const std::vector<observer::observer*>& observers) {
  if (execution.is_cancelled()) {
    execution.set_node_state(node_id, core::task_state::cancelled);
    return core::task_state::cancelled;
  }

  const auto* bp = execution.blueprint();
  if (!bp) return core::task_state::failed;
  const auto* node = bp->find_node(node_id);
  if (!node) return core::task_state::failed;

  if (execution.is_node_completed(node_id)) {
    return core::task_state::success;
  }

  core::retry_policy policy{1, 0, 1.0f};
  if (node->retry) policy = *node->retry;

  std::int32_t attempts = 0;
  do {
    attempts++;
    if (attempts > 1) {
      execution.increment_retry(node_id);
      if (policy.initial_delay_ms > 0) {
        auto delay =
            static_cast<std::int64_t>(policy.initial_delay_ms * std::pow(policy.backoff_multiplier, attempts - 2));
        std::this_thread::sleep_for(std::chrono::milliseconds(delay));
      }
    }
    auto r = execute_node(execution, node_id, registry, observers);
    if (r == core::task_state::success) return r;
    if (execution.is_cancelled()) {
      execution.set_node_state(node_id, core::task_state::cancelled);
      return core::task_state::cancelled;
    }
  } while (attempts < policy.max_attempts);
  return core::task_state::failed;
}

}  // namespace taskflow::engine
