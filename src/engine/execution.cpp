#include "taskflow/engine/execution.hpp"

#include <chrono>

#include "taskflow/core/audit_log.hpp"

namespace taskflow::engine {

workflow_execution::workflow_execution() : state_mutex_(std::make_unique<std::mutex>()) {}

workflow_execution::workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp)
    : workflow_execution(exec_id, std::move(bp), nullptr, nullptr) {}

workflow_execution::workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp,
                                       core::result_storage* result_storage)
    : workflow_execution(exec_id, std::move(bp), result_storage, nullptr) {}

workflow_execution::workflow_execution(std::size_t exec_id, workflow::workflow_blueprint bp,
                                       core::result_storage* result_storage, core::audit_log* audit_log)
    : exec_id_(exec_id),
      blueprint_(std::make_unique<workflow::workflow_blueprint>(std::move(bp))),
      results_(result_storage),
      audit_log_(audit_log),
      state_mutex_(std::make_unique<std::mutex>()) {
  ctx_.set_exec_id(exec_id_);
  ctx_.set_collector(&results_);
  init_node_states();
}

workflow_execution::workflow_execution(workflow_execution&& o) noexcept
    : exec_id_(o.exec_id_),
      cancel_source_(std::move(o.cancel_source_)),
      blueprint_(std::move(o.blueprint_)),
      node_states_(std::move(o.node_states_)),
      completed_nodes_(std::move(o.completed_nodes_)),
      ctx_(std::move(o.ctx_)),
      results_(std::move(o.results_)),
      start_time_(o.start_time_),
      end_time_(o.end_time_),
      audit_log_(o.audit_log_),
      state_mutex_(std::move(o.state_mutex_)) {
  o.exec_id_ = 0;
  o.start_time_ = 0;
  o.end_time_ = 0;
  o.audit_log_ = nullptr;
}

workflow_execution& workflow_execution::operator=(workflow_execution&& o) noexcept {
  if (this == &o) return *this;
  exec_id_ = o.exec_id_;
  cancel_source_ = std::move(o.cancel_source_);
  blueprint_ = std::move(o.blueprint_);
  node_states_ = std::move(o.node_states_);
  completed_nodes_ = std::move(o.completed_nodes_);
  ctx_ = std::move(o.ctx_);
  results_ = std::move(o.results_);
  start_time_ = o.start_time_;
  end_time_ = o.end_time_;
  audit_log_ = o.audit_log_;
  state_mutex_ = std::move(o.state_mutex_);
  o.exec_id_ = 0;
  o.start_time_ = 0;
  o.end_time_ = 0;
  o.audit_log_ = nullptr;
  return *this;
}

std::size_t workflow_execution::id() const noexcept { return exec_id_; }

const workflow::workflow_blueprint* workflow_execution::blueprint() const noexcept { return blueprint_.get(); }

core::node_state workflow_execution::get_node_state(std::size_t node_id) const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  auto it = node_states_.find(node_id);
  if (it != node_states_.end()) return it->second;
  core::node_state ns;
  ns.id = node_id;
  return ns;
}

void workflow_execution::set_node_state(std::size_t node_id, core::task_state state) {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  core::task_state old = core::task_state::pending;
  auto it_old = node_states_.find(node_id);
  if (it_old != node_states_.end()) {
    old = it_old->second.state;
  }
  auto& ns = node_states_[node_id];
  ns.id = node_id;
  ns.state = state;
  if (state == core::task_state::running) {
    ns.started_at =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
  }
  if (state == core::task_state::success || state == core::task_state::failed || state == core::task_state::skipped) {
    ns.finished_at =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
  }
  if (audit_log_ && old != state) {
    audit_log_->record(exec_id_, node_id, old, state, "");
  }
}

void workflow_execution::set_node_error(std::size_t node_id, std::string error) {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  auto it = node_states_.find(node_id);
  if (it != node_states_.end()) {
    it->second.error_message = std::move(error);
  }
}

void workflow_execution::increment_retry(std::size_t node_id) {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  node_states_[node_id].retry_count++;
}

std::int32_t workflow_execution::retry_count(std::size_t node_id) const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  auto it = node_states_.find(node_id);
  return it != node_states_.end() ? it->second.retry_count : 0;
}

const std::unordered_map<std::size_t, core::node_state>& workflow_execution::node_states() const noexcept {
  return node_states_;
}

core::task_ctx& workflow_execution::context() noexcept { return ctx_; }
const core::task_ctx& workflow_execution::context() const noexcept { return ctx_; }

std::int64_t workflow_execution::start_time() const noexcept { return start_time_; }
std::int64_t workflow_execution::end_time() const noexcept { return end_time_; }

void workflow_execution::mark_started() {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  if (start_time_ == 0)
    start_time_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

void workflow_execution::mark_completed() {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  if (end_time_ == 0)
    end_time_ =
        std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::system_clock::now().time_since_epoch())
            .count();
}

core::task_state workflow_execution::overall_state() const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  bool has_running = false, has_pending = false, has_failed = false, has_cancelled = false;
  for (const auto& [_, ns] : node_states_) {
    switch (ns.state) {
      case core::task_state::running:
        has_running = true;
        break;
      case core::task_state::pending:
        has_pending = true;
        break;
      case core::task_state::failed:
        has_failed = true;
        break;
      case core::task_state::cancelled:
        has_cancelled = true;
        break;
      case core::task_state::retry:
        has_pending = true;
        break;
      default:
        break;
    }
  }
  if (has_failed) return core::task_state::failed;
  if (has_cancelled) return core::task_state::cancelled;
  if (has_running) return core::task_state::running;
  if (has_pending) return core::task_state::pending;
  return core::task_state::success;
}

bool workflow_execution::is_complete() const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  for (const auto& [_, ns] : node_states_) {
    if (ns.state != core::task_state::success && ns.state != core::task_state::failed &&
        ns.state != core::task_state::skipped && ns.state != core::task_state::cancelled)
      return false;
  }
  return true;
}

std::size_t workflow_execution::count_by_state(core::task_state state) const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  std::size_t count = 0;
  for (const auto& [_, ns] : node_states_)
    if (ns.state == state) count++;
  return count;
}

result_collector& workflow_execution::results() noexcept { return results_; }
const result_collector& workflow_execution::results() const noexcept { return results_; }

bool workflow_execution::is_node_completed(std::size_t node_id) const {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  return completed_nodes_.find({exec_id_, node_id}) != completed_nodes_.end();
}

void workflow_execution::mark_node_completed(std::size_t node_id) {
  std::lock_guard<std::mutex> lock(*state_mutex_);
  completed_nodes_.insert({exec_id_, node_id});
}

void workflow_execution::init_node_states() {
  if (!blueprint_) return;
  for (const auto& [id, _] : blueprint_->nodes()) {
    core::node_state ns;
    ns.id = id;
    node_states_[id] = ns;
  }
}

void workflow_execution::cancel() { cancel_source_.cancel(); }

bool workflow_execution::is_cancelled() const noexcept { return cancel_source_.is_cancelled(); }

core::cancellation_token workflow_execution::token() const { return cancel_source_.token(); }

}  // namespace taskflow::engine
