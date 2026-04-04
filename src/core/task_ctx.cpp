#include "taskflow/core/task_ctx.hpp"

#include <algorithm>
#include <utility>
#include <vector>

namespace taskflow::core {

namespace {

thread_local std::vector<std::pair<task_ctx*, std::size_t>> invoke_stack;

}  // namespace

task_ctx::task_ctx() : data_mutex_(std::make_unique<std::mutex>()) {}

task_ctx::task_ctx(task_ctx&& other) noexcept
    : data_mutex_(std::move(other.data_mutex_)),
      data_(std::move(other.data_)),
      progress_(other.progress_.load()),
      cancelled_(other.cancelled_.load()),
      cancellation_token_(std::move(other.cancellation_token_)),
      node_id_(other.node_id_),
      exec_id_(other.exec_id_),
      exec_start_time_(other.exec_start_time_),
      collector_(other.collector_) {}

task_ctx& task_ctx::operator=(task_ctx&& other) noexcept {
  if (this != &other) {
    data_mutex_ = std::move(other.data_mutex_);
    data_ = std::move(other.data_);
    progress_.store(other.progress_.load());
    cancelled_.store(other.cancelled_.load());
    cancellation_token_ = std::move(other.cancellation_token_);
    node_id_ = other.node_id_;
    exec_id_ = other.exec_id_;
    exec_start_time_ = other.exec_start_time_;
    collector_ = other.collector_;
  }
  return *this;
}

bool task_ctx::contains(std::string_view key) const {
  std::lock_guard<std::mutex> lock(*data_mutex_);
  return data_.find(std::string(key)) != data_.end();
}

const std::unordered_map<std::string, std::any>& task_ctx::data() const noexcept { return data_; }

std::unordered_map<std::string, std::any>& task_ctx::data() noexcept { return data_; }

void task_ctx::set_data(std::unordered_map<std::string, std::any> d) {
  std::lock_guard<std::mutex> lock(*data_mutex_);
  data_ = std::move(d);
}

void task_ctx::report_progress(float progress) { progress_.store(std::clamp(progress, 0.0f, 1.0f)); }

float task_ctx::progress() const noexcept { return progress_.load(); }

void task_ctx::cancel() { cancelled_.store(true); }

bool task_ctx::is_cancelled() const noexcept { return cancelled_.load(); }

std::size_t task_ctx::node_id() const noexcept {
  for (auto it = invoke_stack.rbegin(); it != invoke_stack.rend(); ++it) {
    if (it->first == this) return it->second;
  }
  return node_id_;
}

void task_ctx::set_node_id(std::size_t id) noexcept { node_id_ = id; }

std::size_t task_ctx::exec_id() const noexcept { return exec_id_; }

void task_ctx::set_exec_id(std::size_t id) noexcept { exec_id_ = id; }

std::chrono::system_clock::time_point task_ctx::exec_start_time() const noexcept { return exec_start_time_; }

void task_ctx::set_exec_start_time(std::chrono::system_clock::time_point t) noexcept { exec_start_time_ = t; }

void task_ctx::ensure_exec_start_time(std::chrono::system_clock::time_point t) {
  std::lock_guard<std::mutex> lock(*data_mutex_);
  if (exec_start_time_ == std::chrono::system_clock::time_point{}) {
    exec_start_time_ = t;
  }
}

void task_ctx::set_collector(engine::result_collector* collector) noexcept { collector_ = collector; }

void task_ctx::set_cancellation_token(cancellation_token token) { cancellation_token_ = std::move(token); }

const cancellation_token& task_ctx::get_cancellation_token() const noexcept { return cancellation_token_; }

task_ctx_invoke_scope::task_ctx_invoke_scope(task_ctx& ctx, std::size_t node_id) : ctx_(&ctx) {
  invoke_stack.push_back({&ctx, node_id});
}

task_ctx_invoke_scope::~task_ctx_invoke_scope() {
  if (!invoke_stack.empty() && invoke_stack.back().first == ctx_) {
    invoke_stack.pop_back();
  }
}

}  // namespace taskflow::core
