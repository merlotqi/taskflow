#include "taskflow/engine/orchestrator.hpp"

#include <algorithm>
#include <chrono>
#include <string>
#include <system_error>

#include "taskflow/core/error_code.hpp"
#include "taskflow/core/state_storage.hpp"
#include "taskflow/engine/default_thread_pool.hpp"
#include "taskflow/engine/executor.hpp"
#include "taskflow/engine/scheduler.hpp"
#include "taskflow/obs/hooks_observer.hpp"
#include "taskflow/obs/observer.hpp"

namespace taskflow::engine {

orchestrator::orchestrator() : executor_(std::make_unique<default_thread_pool>()) {}

orchestrator::orchestrator(std::unique_ptr<parallel_executor> executor) : executor_(std::move(executor)) {
  if (!executor_) executor_ = std::make_unique<default_thread_pool>();
}

orchestrator::orchestrator(std::unique_ptr<core::state_storage> storage)
    : storage_(std::move(storage)), executor_(std::make_unique<default_thread_pool>()) {}

orchestrator::orchestrator(std::unique_ptr<parallel_executor> executor, std::unique_ptr<core::state_storage> storage)
    : storage_(std::move(storage)), executor_(std::move(executor)) {
  if (!executor_) executor_ = std::make_unique<default_thread_pool>();
}

orchestrator::orchestrator(std::unique_ptr<core::result_storage> result_storage)
    : result_storage_(std::move(result_storage)), executor_(std::make_unique<default_thread_pool>()) {}

orchestrator::orchestrator(std::unique_ptr<parallel_executor> executor,
                           std::unique_ptr<core::result_storage> result_storage)
    : result_storage_(std::move(result_storage)), executor_(std::move(executor)) {
  if (!executor_) executor_ = std::make_unique<default_thread_pool>();
}

void orchestrator::set_audit_log(std::shared_ptr<core::audit_log> log) { audit_log_ = std::move(log); }

void orchestrator::set_event_hooks(integration::workflow_event_hooks hooks) { event_hooks_ = std::move(hooks); }

void orchestrator::register_task(std::string type_name, core::task_factory factory) {
  registry_.register_task(std::move(type_name), std::move(factory));
}

const task_registry& orchestrator::registry() const noexcept { return registry_; }

void orchestrator::register_blueprint(std::size_t id, workflow::workflow_blueprint bp) {
  blueprints_[id] = std::move(bp);
}

void orchestrator::register_blueprint(std::string_view name, workflow::workflow_blueprint bp) {
  named_blueprints_[std::string(name)] = std::move(bp);
}

const workflow::workflow_blueprint* orchestrator::get_blueprint(std::size_t id) const noexcept {
  auto it = blueprints_.find(id);
  return it != blueprints_.end() ? &it->second : nullptr;
}

const workflow::workflow_blueprint* orchestrator::get_blueprint(std::string_view name) const noexcept {
  auto it = named_blueprints_.find(std::string(name));
  return it != named_blueprints_.end() ? &it->second : nullptr;
}

bool orchestrator::has_blueprint(std::size_t id) const noexcept { return blueprints_.find(id) != blueprints_.end(); }

bool orchestrator::has_blueprint(std::string_view name) const noexcept {
  return named_blueprints_.find(std::string(name)) != named_blueprints_.end();
}

std::size_t orchestrator::allocate_exec_id() { return next_exec_id_.fetch_add(1) + 1; }

std::size_t orchestrator::create_execution_from_blueprint(const workflow::workflow_blueprint& src) {
  auto exec_id = allocate_exec_id();
  workflow::workflow_blueprint copy;
  for (const auto& [nid, n] : src.nodes()) copy.add_node(n);
  for (const auto& e : src.edges()) copy.add_edge(e);
  core::result_storage* rs = result_storage_.get();
  executions_.emplace(exec_id, workflow_execution(exec_id, std::move(copy), rs, audit_log_.get()));
  if (storage_) {
    if (auto* ex = get_execution(exec_id)) {
      storage_->save(exec_id, ex->to_snapshot_json());
    }
  }
  return exec_id;
}

std::size_t orchestrator::create_execution(std::size_t blueprint_id) {
  auto it = blueprints_.find(blueprint_id);
  if (it == blueprints_.end()) {
    throw std::system_error(core::make_error_code(core::errc::blueprint_not_found));
  }
  return create_execution_from_blueprint(it->second);
}

std::size_t orchestrator::create_execution(std::string_view blueprint_name) {
  auto it = named_blueprints_.find(std::string(blueprint_name));
  if (it == named_blueprints_.end()) {
    throw std::system_error(core::make_error_code(core::errc::blueprint_not_found));
  }
  return create_execution_from_blueprint(it->second);
}

workflow_execution* orchestrator::get_execution(std::size_t id) noexcept {
  auto it = executions_.find(id);
  return it != executions_.end() ? &it->second : nullptr;
}

const workflow_execution* orchestrator::get_execution(std::size_t id) const noexcept {
  auto it = executions_.find(id);
  return it != executions_.end() ? &it->second : nullptr;
}

void orchestrator::add_observer(obs::observer* obs) {
  if (obs) observers_.push_back(obs);
}

void orchestrator::remove_observer(obs::observer* obs) {
  observers_.erase(std::remove(observers_.begin(), observers_.end(), obs), observers_.end());
}

core::task_state orchestrator::run_sync(std::size_t execution_id, bool stop_on_first_failure) {
  auto* exec = get_execution(execution_id);
  if (!exec) {
    throw std::system_error(core::make_error_code(core::errc::execution_not_found));
  }

  const auto* bp = exec->blueprint();
  if (!bp) return core::task_state::failed;
  auto err = bp->validate();
  if (!err.empty()) {
    throw std::system_error(core::make_error_code(core::errc::invalid_blueprint));
  }

  exec->mark_started();

  const integration::workflow_event_hooks* hooks_ptr = event_hooks_ ? &*event_hooks_ : nullptr;
  obs::hooks_observer hook_forward{hooks_ptr};
  std::vector<obs::observer*> exec_observers;
  exec_observers.reserve((hooks_ptr ? 1u : 0u) + observers_.size());
  if (hooks_ptr) exec_observers.push_back(&hook_forward);
  exec_observers.insert(exec_observers.end(), observers_.begin(), observers_.end());

  bool failed = false;
  while (true) {
    if (failed && stop_on_first_failure) break;

    if (exec->is_cancelled()) break;

    if (exec->is_complete()) break;

    auto ready = scheduler::ready_nodes(*exec);
    if (ready.empty()) {
      if (exec->is_complete()) break;
      break;
    }

    for (auto nid : ready) {
      for (auto* o : exec_observers)
        if (o) o->on_node_ready(execution_id, nid);
      executor_->submit([this, exec, nid, exec_observers]() {
        (void)executor::execute_with_retry(*exec, nid, registry_, exec_observers);
      });
    }
    executor_->wait_all();

    if (storage_) {
      storage_->save(execution_id, exec->to_snapshot_json());
    }

    for (auto nid : ready) {
      if (exec->get_node_state(nid).state == core::task_state::failed) {
        failed = true;
        break;
      }
    }
  }

  if (failed && stop_on_first_failure) {
    for (const auto& [nid, _] : bp->nodes()) {
      if (exec->get_node_state(nid).state == core::task_state::pending) {
        exec->set_node_state(nid, core::task_state::skipped);
      }
    }
  }

  if (exec->is_cancelled()) {
    for (const auto& [nid, _] : bp->nodes()) {
      auto state = exec->get_node_state(nid).state;
      if (state == core::task_state::pending || state == core::task_state::running) {
        exec->set_node_state(nid, core::task_state::cancelled);
      }
    }
  }

  exec->mark_completed();
  auto overall = exec->overall_state();
  auto dur = std::chrono::duration_cast<std::chrono::milliseconds>(exec->end_time() - exec->start_time());
  for (auto* o : observers_)
    if (o) o->on_workflow_complete(execution_id, overall, dur);
  if (hooks_ptr) hook_forward.on_workflow_complete(execution_id, overall, dur);
  return overall;
}

std::pair<std::size_t, core::task_state> orchestrator::run_sync_from_blueprint(std::size_t blueprint_id,
                                                                               bool stop_on_first_failure) {
  auto exec_id = create_execution(blueprint_id);
  return {exec_id, run_sync(exec_id, stop_on_first_failure)};
}

parallel_executor* orchestrator::executor() noexcept { return executor_.get(); }

const parallel_executor* orchestrator::executor() const noexcept { return executor_.get(); }

bool orchestrator::cleanup_execution(std::size_t execution_id) {
  auto it = executions_.find(execution_id);
  if (it == executions_.end()) return false;
  executions_.erase(it);
  return true;
}

std::size_t orchestrator::cleanup_completed_executions() {
  std::size_t count = 0;
  for (auto it = executions_.begin(); it != executions_.end();) {
    if (it->second.is_complete()) {
      it = executions_.erase(it);
      ++count;
    } else {
      ++it;
    }
  }
  return count;
}

std::size_t orchestrator::cleanup_old_executions(std::chrono::milliseconds older_than) {
  const auto now = std::chrono::system_clock::now();
  std::size_t count = 0;
  for (auto it = executions_.begin(); it != executions_.end();) {
    if (it->second.is_complete() && (now - it->second.end_time()) > older_than) {
      it = executions_.erase(it);
      ++count;
    } else {
      ++it;
    }
  }
  return count;
}

std::future<core::task_state> orchestrator::run_async(std::size_t execution_id, bool stop_on_first_failure) {
  return std::async(std::launch::async, [this, execution_id, stop_on_first_failure]() {
    return run_sync(execution_id, stop_on_first_failure);
  });
}

std::pair<std::size_t, std::future<core::task_state>> orchestrator::run_async_from_blueprint(
    std::size_t blueprint_id, bool stop_on_first_failure) {
  auto exec_id = create_execution(blueprint_id);
  return {exec_id, run_async(exec_id, stop_on_first_failure)};
}

bool orchestrator::cancel_execution(std::size_t execution_id) {
  auto* exec = get_execution(execution_id);
  if (!exec) {
    return false;
  }
  exec->cancel();
  return true;
}

}  // namespace taskflow::engine
