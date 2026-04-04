#pragma once

#include <atomic>
#include <chrono>
#include <cstddef>
#include <future>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

#include "taskflow/engine/execution.hpp"
#include "taskflow/engine/parallel_executor.hpp"
#include "taskflow/engine/registry.hpp"
#include "taskflow/integration/event_hooks.hpp"

namespace taskflow::core {
class audit_log;
class result_storage;
class state_storage;
}  // namespace taskflow::core

namespace taskflow::obs {
class observer;
}

namespace taskflow::engine {

class orchestrator {
 public:
  orchestrator();
  explicit orchestrator(std::unique_ptr<parallel_executor> executor);
  explicit orchestrator(std::unique_ptr<core::state_storage> storage);
  explicit orchestrator(std::unique_ptr<core::result_storage> result_storage);
  orchestrator(std::unique_ptr<parallel_executor> executor, std::unique_ptr<core::state_storage> storage);
  orchestrator(std::unique_ptr<parallel_executor> executor, std::unique_ptr<core::result_storage> result_storage);

  orchestrator(const orchestrator&) = delete;
  orchestrator& operator=(const orchestrator&) = delete;
  orchestrator(orchestrator&&) noexcept = delete;
  orchestrator& operator=(orchestrator&&) noexcept = delete;

  void register_task(std::string type_name, core::task_factory factory);
  template <typename T, std::enable_if_t<core::is_task_v<T>, int> = 0>
  void register_task(std::string type_name) {
    registry_.register_task<T>(std::move(type_name));
  }
  [[nodiscard]] const task_registry& registry() const noexcept;

  void set_audit_log(std::shared_ptr<core::audit_log> log);
  void set_event_hooks(integration::workflow_event_hooks hooks);

  void register_blueprint(std::size_t id, workflow::workflow_blueprint bp);
  void register_blueprint(std::string_view name, workflow::workflow_blueprint bp);
  [[nodiscard]] const workflow::workflow_blueprint* get_blueprint(std::size_t id) const noexcept;
  [[nodiscard]] const workflow::workflow_blueprint* get_blueprint(std::string_view name) const noexcept;
  [[nodiscard]] bool has_blueprint(std::size_t id) const noexcept;
  [[nodiscard]] bool has_blueprint(std::string_view name) const noexcept;

  std::size_t create_execution(std::size_t blueprint_id);
  std::size_t create_execution(std::string_view blueprint_name);

  [[nodiscard]] workflow_execution* get_execution(std::size_t id) noexcept;
  [[nodiscard]] const workflow_execution* get_execution(std::size_t id) const noexcept;

  // Cleanup methods
  bool cleanup_execution(std::size_t execution_id);
  std::size_t cleanup_completed_executions();
  std::size_t cleanup_old_executions(std::chrono::milliseconds older_than);

  void add_observer(obs::observer* obs);
  void remove_observer(obs::observer* obs);

  [[nodiscard]] core::task_state run_sync(std::size_t execution_id, bool stop_on_first_failure = true);
  std::pair<std::size_t, core::task_state> run_sync_from_blueprint(std::size_t blueprint_id,
                                                                   bool stop_on_first_failure = true);

  // Async execution
  std::future<core::task_state> run_async(std::size_t execution_id, bool stop_on_first_failure = true);
  std::pair<std::size_t, std::future<core::task_state>> run_async_from_blueprint(std::size_t blueprint_id,
                                                                                 bool stop_on_first_failure = true);

  // Cancellation
  bool cancel_execution(std::size_t execution_id);

  [[nodiscard]] parallel_executor* executor() noexcept;
  [[nodiscard]] const parallel_executor* executor() const noexcept;

 private:
  task_registry registry_;
  std::unordered_map<std::size_t, workflow::workflow_blueprint> blueprints_;
  std::unordered_map<std::string, workflow::workflow_blueprint> named_blueprints_;
  std::unordered_map<std::size_t, workflow_execution> executions_;
  std::vector<obs::observer*> observers_;
  std::unique_ptr<core::state_storage> storage_;
  std::unique_ptr<core::result_storage> result_storage_;
  std::unique_ptr<parallel_executor> executor_;
  std::shared_ptr<core::audit_log> audit_log_;
  std::optional<integration::workflow_event_hooks> event_hooks_;

  std::atomic<std::size_t> next_exec_id_{0};
  mutable std::unique_ptr<std::mutex> state_mutex_;

  /// Inserts into `executions_` and optionally persists; caller must hold `state_mutex_`.
  std::size_t create_execution_from_blueprint(const workflow::workflow_blueprint& src);
  std::size_t allocate_exec_id();
};

}  // namespace taskflow::engine
