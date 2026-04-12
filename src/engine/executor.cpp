#include "taskflow/engine/executor.hpp"

#include <chrono>
#include <cmath>
#include <cstdlib>
#include <random>
#include <thread>

#include "taskflow/core/task_ctx.hpp"
#include "taskflow/engine/execution.hpp"
#include "taskflow/engine/registry.hpp"
#include "taskflow/obs/observer.hpp"
#include "taskflow/workflow/node.hpp"

namespace taskflow::engine {

core::task_state executor::execute_node(workflow_execution& execution, std::size_t node_id,
                                        const itask_registry& registry, const std::vector<obs::observer*>& observers) {
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

  if (!execution.try_transition_node_state(node_id, core::task_state::pending, core::task_state::running)) {
    if (!execution.try_transition_node_state(node_id, core::task_state::retry, core::task_state::running)) {
      return execution.get_node_state(node_id).state;
    }
  }

  auto start = std::chrono::system_clock::now();
  for (auto* obs : observers)
    if (obs) obs->on_task_start(execution.id(), node_id, node->task_type, execution.retry_count(node_id) + 1);

  auto& ctx = execution.context();
  core::task_ctx_invoke_scope invoke_scope(ctx, node_id);
  ctx.ensure_exec_start_time(start);

  core::task_state result = core::task_state::failed;
  try {
    result = task.execute(ctx);
  } catch (const std::exception& e) {
    execution.set_node_error(node_id, e.what());
  }

  auto end = std::chrono::system_clock::now();
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);
  if (result == core::task_state::success) {
    execution.set_node_state(node_id, core::task_state::success);
    execution.mark_node_completed(node_id);
    execution.record_forward_success(node_id);
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
                                              const std::vector<obs::observer*>& observers) {
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

  core::retry_policy policy{};
  policy.max_attempts = 1;
  policy.initial_delay = std::chrono::milliseconds{0};
  policy.backoff_multiplier = 1.0f;
  if (node->retry) policy = *node->retry;

  std::int32_t attempts = 0;
  do {
    attempts++;
    if (attempts > 1) {
      if (!execution.try_transition_node_state(node_id, core::task_state::failed, core::task_state::retry)) {
        return execution.get_node_state(node_id).state;
      }
      execution.increment_retry(node_id);

      std::chrono::milliseconds delay{0};
      if (policy.initial_delay.count() > 0) {
        // Calculate delay with exponential backoff
        auto delay_ms =
            static_cast<std::int64_t>(policy.initial_delay.count() * std::pow(policy.backoff_multiplier, attempts - 2));
        delay = std::chrono::milliseconds(delay_ms);

        // Apply max delay cap
        if (policy.max_delay.count() > 0 && delay > policy.max_delay) {
          delay = policy.max_delay;
        }

        // Apply jitter if enabled
        if (policy.jitter && policy.jitter_range.count() > 0) {
          static thread_local std::mt19937 gen(std::random_device{}());
          std::uniform_int_distribution<std::int64_t> dist(0, policy.jitter_range.count());
          delay += std::chrono::milliseconds(dist(gen));
        }
      }

      // Notify observers about retry
      const auto* node = bp->find_node(node_id);
      if (node) {
        for (auto* obs : observers)
          if (obs) obs->on_task_retry(execution.id(), node_id, node->task_type, attempts, delay);
      }

      if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
      }
    }

    auto r = execute_node(execution, node_id, registry, observers);
    if (r == core::task_state::success) return r;
    if (execution.is_cancelled()) {
      execution.set_node_state(node_id, core::task_state::cancelled);
      return core::task_state::cancelled;
    }

    // Check custom retry condition if provided
    if (policy.should_retry) {
      const auto& ns = execution.get_node_state(node_id);
      if (!(*policy.should_retry)(ns.error_message, r)) {
        return core::task_state::failed;  // Don't retry based on condition
      }
    }
  } while (attempts < policy.max_attempts);
  return core::task_state::failed;
}

core::task_state executor::execute_compensation(workflow_execution& execution, std::size_t node_id,
                                                const itask_registry& registry,
                                                const std::vector<obs::observer*>& observers, bool respect_cancel) {
  if (respect_cancel && execution.is_cancelled()) {
    return core::task_state::cancelled;
  }

  const auto* bp = execution.blueprint();
  if (!bp) return core::task_state::failed;
  const auto* node = bp->find_node(node_id);
  if (!node) return core::task_state::failed;

  if (!node->compensate_task_type || node->compensate_task_type->empty()) {
    return core::task_state::success;
  }

  if (!execution.try_transition_node_state(node_id, core::task_state::success, core::task_state::compensating)) {
    return execution.get_node_state(node_id).state;
  }

  const std::string& comp_type = *node->compensate_task_type;
  std::string_view comp_sv(comp_type);

  core::retry_policy policy{};
  policy.max_attempts = 1;
  policy.initial_delay = std::chrono::milliseconds{0};
  policy.backoff_multiplier = 1.0f;
  if (node->compensate_retry) policy = *node->compensate_retry;

  std::int32_t attempts = 0;
  do {
    attempts++;
    if (attempts > 1) {
      if (respect_cancel && execution.is_cancelled()) {
        execution.set_node_state(node_id, core::task_state::cancelled);
        return core::task_state::cancelled;
      }

      std::chrono::milliseconds delay{0};
      if (policy.initial_delay.count() > 0) {
        auto delay_ms =
            static_cast<std::int64_t>(policy.initial_delay.count() * std::pow(policy.backoff_multiplier, attempts - 2));
        delay = std::chrono::milliseconds(delay_ms);
        if (policy.max_delay.count() > 0 && delay > policy.max_delay) {
          delay = policy.max_delay;
        }
        if (policy.jitter && policy.jitter_range.count() > 0) {
          static thread_local std::mt19937 gen(std::random_device{}());
          std::uniform_int_distribution<std::int64_t> dist(0, policy.jitter_range.count());
          delay += std::chrono::milliseconds(dist(gen));
        }
      }
      if (delay.count() > 0) {
        std::this_thread::sleep_for(delay);
      }
    }

    auto task = registry.create(comp_type);
    if (!task) {
      execution.set_node_error(node_id, "unknown compensate task type: " + comp_type);
      execution.set_node_state(node_id, core::task_state::compensation_failed);
      return core::task_state::compensation_failed;
    }

    auto start = std::chrono::system_clock::now();
    for (auto* obs : observers)
      if (obs) obs->on_compensation_start(execution.id(), node_id, comp_sv, attempts);

    auto& ctx = execution.context();
    core::task_ctx_invoke_scope invoke_scope(ctx, node_id);
    ctx.ensure_exec_start_time(start);

    core::task_state result = core::task_state::failed;
    try {
      result = task.execute(ctx);
    } catch (const std::exception& e) {
      execution.set_node_error(node_id, e.what());
    }

    auto end = std::chrono::system_clock::now();
    auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(end - start);

    if (result == core::task_state::success) {
      execution.set_node_state(node_id, core::task_state::compensated);
      for (auto* obs : observers)
        if (obs) obs->on_compensation_complete(execution.id(), node_id, comp_sv, dur);
      return core::task_state::compensated;
    }

    if (respect_cancel && execution.is_cancelled()) {
      execution.set_node_state(node_id, core::task_state::cancelled);
      return core::task_state::cancelled;
    }

    for (auto* obs : observers)
      if (obs)
        obs->on_compensation_fail(execution.id(), node_id, comp_sv, execution.get_node_state(node_id).error_message,
                                  dur);

    if (policy.should_retry) {
      const auto& ns = execution.get_node_state(node_id);
      if (!(*policy.should_retry)(ns.error_message, result)) {
        execution.set_node_state(node_id, core::task_state::compensation_failed);
        return core::task_state::compensation_failed;
      }
    }
  } while (attempts < policy.max_attempts);

  execution.set_node_state(node_id, core::task_state::compensation_failed);
  return core::task_state::compensation_failed;
}

}  // namespace taskflow::engine
