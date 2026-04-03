#pragma once

#include "taskflow/integration/event_hooks.hpp"
#include "taskflow/obs/observer.hpp"

namespace taskflow::obs {

/// Adapts integration::workflow_event_hooks to the observer interface so the executor
/// only walks one observer list. Used internally by orchestrator::set_event_hooks.
class hooks_observer final : public observer {
 public:
  explicit hooks_observer(const integration::workflow_event_hooks* hooks) noexcept : hooks_(hooks) {}

  void on_node_ready(std::size_t exec_id, std::size_t node_id) noexcept override {
    if (hooks_ && hooks_->on_node_ready) hooks_->on_node_ready(exec_id, node_id);
  }

  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view /*task_type*/,
                     std::int32_t /*attempt*/) noexcept override {
    if (hooks_ && hooks_->on_node_started) hooks_->on_node_started(exec_id, node_id);
  }

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view /*task_type*/,
                        std::chrono::milliseconds /*duration_ms*/) noexcept override {
    if (hooks_ && hooks_->on_node_finished) hooks_->on_node_finished(exec_id, node_id, true);
  }

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view /*task_type*/,
                    std::string_view /*error*/, std::chrono::milliseconds /*duration_ms*/) noexcept override {
    if (hooks_ && hooks_->on_node_finished) hooks_->on_node_finished(exec_id, node_id, false);
  }

  void on_workflow_complete(std::size_t exec_id, core::task_state state,
                            std::chrono::milliseconds /*duration_ms*/) noexcept override {
    if (hooks_ && hooks_->on_workflow_finished)
      hooks_->on_workflow_finished(exec_id, state == core::task_state::success);
  }

 private:
  const integration::workflow_event_hooks* hooks_;
};

}  // namespace taskflow::obs
