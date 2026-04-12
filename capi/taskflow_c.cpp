#include "taskflow_c.h"

#include <atomic>
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <future>
#include <memory>
#include <mutex>
#include <new>
#include <string>
#include <string_view>
#include <system_error>
#include <taskflow/taskflow.hpp>
#include <unordered_map>
#include <utility>

#ifndef TASKFLOW_CAPI_VERSION_STRING
#define TASKFLOW_CAPI_VERSION_STRING "dev"
#endif

namespace {

using taskflow::core::retry_policy;
using taskflow::core::task_ctx;
using taskflow::core::task_state;
using taskflow::engine::orchestrator_run_options;
using taskflow::engine::workflow_execution;
using taskflow::workflow::workflow_blueprint;

std::mutex g_state_mutex;
std::atomic<uint64_t> g_next_handle{1};

struct CallbackState {
  std::mutex mutex;
  tf_event_callback_fn callback = nullptr;
  void* userdata = nullptr;
};

struct TaskCallbackRegistration {
  tf_task_callback_fn callback = nullptr;
  void* userdata = nullptr;
};

struct EdgeConditionRegistration {
  tf_edge_condition_callback_fn callback = nullptr;
  void* userdata = nullptr;
};

struct TaskContextView {
  task_ctx* ctx = nullptr;
  workflow_execution* execution = nullptr;
  std::size_t node_id = 0;
};

struct AsyncExecutionState {
  std::mutex mutex;
  std::shared_future<task_state> future;
  bool has_future = false;
  bool resolved = false;
  TF_Error resolved_error = TF_OK;
  task_state resolved_state = task_state::pending;
};

struct BlueprintState {
  workflow_blueprint blueprint;
};

class c_api_observer final : public taskflow::obs::observer {
 public:
  explicit c_api_observer(std::shared_ptr<CallbackState> callback_state) : callback_state_(std::move(callback_state)) {}

  void on_node_ready(std::size_t exec_id, std::size_t node_id) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_NODE_READY, 0, nullptr);
  }

  void on_task_start(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                     std::int32_t /*attempt*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_NODE_STARTED, 0, ToCString(task_type));
  }

  void on_task_complete(std::size_t exec_id, std::size_t node_id, std::string_view task_type,
                        std::chrono::milliseconds /*duration_ms*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_NODE_FINISHED, 1, ToCString(task_type));
  }

  void on_task_fail(std::size_t exec_id, std::size_t node_id, std::string_view task_type, std::string_view /*error*/,
                    std::chrono::milliseconds /*duration_ms*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_NODE_FINISHED, 0, ToCString(task_type));
  }

  void on_workflow_complete(std::size_t exec_id, task_state state,
                            std::chrono::milliseconds /*duration_ms*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, 0, TF_EVENT_WORKFLOW_FINISHED, state == task_state::success ? 1 : 0,
                  nullptr);
  }

  void on_compensation_start(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                             std::int32_t /*attempt*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_COMPENSATION_STARTED, 0, ToCString(compensate_task_type));
  }

  void on_compensation_complete(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                                std::chrono::milliseconds /*duration_ms*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_COMPENSATION_FINISHED, 1,
                  ToCString(compensate_task_type));
  }

  void on_compensation_fail(std::size_t exec_id, std::size_t node_id, std::string_view compensate_task_type,
                            std::string_view /*error*/, std::chrono::milliseconds /*duration_ms*/) noexcept override {
    DispatchEvent(callback_state_, exec_id, node_id, TF_EVENT_COMPENSATION_FINISHED, 0,
                  ToCString(compensate_task_type));
  }

 private:
  static const char* ToCString(std::string_view value) noexcept { return value.empty() ? nullptr : value.data(); }

  static void DispatchEvent(const std::shared_ptr<CallbackState>& callback_state, uint64_t exec_id, uint64_t node_id,
                            int event_type, int success, const char* task_type) noexcept {
    if (!callback_state) {
      return;
    }

    tf_event_callback_fn callback = nullptr;
    void* userdata = nullptr;
    {
      std::lock_guard<std::mutex> lock(callback_state->mutex);
      callback = callback_state->callback;
      userdata = callback_state->userdata;
    }

    if (callback) {
      callback(exec_id, node_id, event_type, success, task_type, userdata);
    }
  }

  std::shared_ptr<CallbackState> callback_state_;
};

struct OrchestratorState {
  std::unique_ptr<taskflow::engine::orchestrator> orchestrator;
  std::shared_ptr<CallbackState> callback_state;
  std::unique_ptr<c_api_observer> observer;
  bool destroyed = false;

  ~OrchestratorState() {
    if (orchestrator && observer) {
      orchestrator->remove_observer(observer.get());
    }
  }
};

struct ExecutionState {
  std::shared_ptr<OrchestratorState> orchestrator_state;
  uint64_t execution_id = 0;
  std::shared_ptr<AsyncExecutionState> async_state;
};

std::unordered_map<uint64_t, std::shared_ptr<BlueprintState>> g_blueprints;
std::unordered_map<uint64_t, std::shared_ptr<OrchestratorState>> g_orchestrators;
std::unordered_map<uint64_t, ExecutionState> g_executions;

uint64_t NextHandle() { return g_next_handle.fetch_add(1, std::memory_order_relaxed); }

TF_Error MapErrorCode(const std::error_code& code) {
  using taskflow::core::errc;
  using taskflow::core::make_error_code;

  if (code == make_error_code(errc::blueprint_not_found)) {
    return TF_ERROR_BLUEPRINT_NOT_FOUND;
  }
  if (code == make_error_code(errc::execution_not_found)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  if (code == make_error_code(errc::invalid_blueprint) || code == make_error_code(errc::invalid_state_transition)) {
    return TF_ERROR_INVALID_STATE;
  }
  if (code == make_error_code(errc::task_type_not_found)) {
    return TF_ERROR_TASK_TYPE_NOT_FOUND;
  }
  if (code == make_error_code(errc::storage_error)) {
    return TF_ERROR_STORAGE;
  }
  if (code == make_error_code(errc::operation_not_permitted)) {
    return TF_ERROR_OPERATION_NOT_PERMITTED;
  }

  return TF_ERROR_UNKNOWN;
}

TF_Error MapCurrentException() {
  try {
    throw;
  } catch (const std::system_error& error) {
    return MapErrorCode(error.code());
  } catch (const std::bad_alloc&) {
    return TF_ERROR_UNKNOWN;
  } catch (...) {
    return TF_ERROR_UNKNOWN;
  }
}

bool ParseTaskState(int raw_state, task_state* out_state) {
  if (!out_state) {
    return false;
  }

  switch (raw_state) {
    case TF_TASK_STATE_PENDING:
      *out_state = task_state::pending;
      return true;
    case TF_TASK_STATE_RUNNING:
      *out_state = task_state::running;
      return true;
    case TF_TASK_STATE_SUCCESS:
      *out_state = task_state::success;
      return true;
    case TF_TASK_STATE_FAILED:
      *out_state = task_state::failed;
      return true;
    case TF_TASK_STATE_RETRY:
      *out_state = task_state::retry;
      return true;
    case TF_TASK_STATE_SKIPPED:
      *out_state = task_state::skipped;
      return true;
    case TF_TASK_STATE_CANCELLED:
      *out_state = task_state::cancelled;
      return true;
    case TF_TASK_STATE_COMPENSATING:
      *out_state = task_state::compensating;
      return true;
    case TF_TASK_STATE_COMPENSATED:
      *out_state = task_state::compensated;
      return true;
    case TF_TASK_STATE_COMPENSATION_FAILED:
      *out_state = task_state::compensation_failed;
      return true;
    default:
      return false;
  }
}

int ToTaskState(task_state state) { return static_cast<int>(state); }

task_state FromCallbackTaskState(int raw_state, workflow_execution* execution, std::size_t node_id) {
  task_state parsed = task_state::failed;
  if (ParseTaskState(raw_state, &parsed)) {
    return parsed;
  }

  if (execution) {
    execution->set_node_error(node_id, "invalid task state returned from C callback");
  }
  return task_state::failed;
}

std::shared_ptr<BlueprintState> FindBlueprint(tf_blueprint_t handle) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  auto it = g_blueprints.find(handle);
  if (it == g_blueprints.end()) {
    return {};
  }
  return it->second;
}

std::shared_ptr<OrchestratorState> FindOrchestrator(tf_orchestrator_t handle) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  auto it = g_orchestrators.find(handle);
  if (it == g_orchestrators.end() || it->second->destroyed) {
    return {};
  }
  return it->second;
}

bool FindExecution(tf_execution_t handle, ExecutionState* out_state) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  auto it = g_executions.find(handle);
  if (it == g_executions.end()) {
    return false;
  }
  *out_state = it->second;
  return true;
}

workflow_execution* ResolveExecution(const ExecutionState& execution_state) {
  if (!execution_state.orchestrator_state || !execution_state.orchestrator_state->orchestrator) {
    return nullptr;
  }
  return execution_state.orchestrator_state->orchestrator->get_execution(execution_state.execution_id);
}

bool NormalizeRunOptions(const TF_RunOptions* raw_options, orchestrator_run_options* out_options) {
  orchestrator_run_options normalized{};

  if (!raw_options) {
    *out_options = normalized;
    return true;
  }

  if (raw_options->struct_size != 0 && raw_options->struct_size < sizeof(TF_RunOptions)) {
    return false;
  }

  normalized.stop_on_first_failure = raw_options->stop_on_first_failure != 0;
  normalized.compensate_on_failure = raw_options->compensate_on_failure != 0;
  normalized.compensate_on_cancel = raw_options->compensate_on_cancel != 0;

  *out_options = normalized;
  return true;
}

bool NormalizeOrchestratorOptions(const TF_OrchestratorOptions* raw_options, bool* out_state_storage,
                                  bool* out_result_storage) {
  bool enable_state_storage = false;
  bool enable_result_storage = false;

  if (raw_options) {
    if (raw_options->struct_size != 0 && raw_options->struct_size < sizeof(TF_OrchestratorOptions)) {
      return false;
    }
    enable_state_storage = raw_options->enable_memory_state_storage != 0;
    enable_result_storage = raw_options->enable_memory_result_storage != 0;
  }

  *out_state_storage = enable_state_storage;
  *out_result_storage = enable_result_storage;
  return true;
}

bool NormalizeRetryPolicy(const TF_RetryPolicy* raw_policy, retry_policy* out_policy) {
  if (!raw_policy || !out_policy) {
    return false;
  }

  if (raw_policy->struct_size != 0 && raw_policy->struct_size < sizeof(TF_RetryPolicy)) {
    return false;
  }
  if (raw_policy->max_attempts <= 0 || raw_policy->backoff_multiplier <= 0.0) {
    return false;
  }

  retry_policy normalized{};
  normalized.max_attempts = raw_policy->max_attempts;
  normalized.initial_delay = std::chrono::milliseconds(raw_policy->initial_delay_ms);
  normalized.backoff_multiplier = static_cast<float>(raw_policy->backoff_multiplier);
  normalized.max_delay = std::chrono::milliseconds(raw_policy->max_delay_ms);
  normalized.jitter = raw_policy->jitter != 0;
  normalized.jitter_range = std::chrono::milliseconds(raw_policy->jitter_range_ms);

  *out_policy = normalized;
  return true;
}

workflow_blueprint CloneBlueprint(const workflow_blueprint& source) {
  workflow_blueprint clone;
  for (const auto& [node_id, node] : source.nodes()) {
    (void)node_id;
    clone.add_node(node);
  }
  for (const auto& edge : source.edges()) {
    clone.add_edge(edge);
  }
  return clone;
}

TaskContextView* ToTaskContextView(tf_task_context_t ctx) { return reinterpret_cast<TaskContextView*>(ctx); }

TF_Error CopyStringToBuffer(std::string_view value, char** out_buffer, size_t* out_size) {
  if (!out_buffer) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_buffer = nullptr;
  if (out_size) {
    *out_size = 0;
  }

  char* buffer = static_cast<char*>(std::malloc(value.size() + 1));
  if (!buffer) {
    return TF_ERROR_UNKNOWN;
  }

  if (!value.empty()) {
    std::memcpy(buffer, value.data(), value.size());
  }
  buffer[value.size()] = '\0';

  *out_buffer = buffer;
  if (out_size) {
    *out_size = value.size();
  }
  return TF_OK;
}

template <typename T>
TF_Error SetContextValue(task_ctx* ctx, const char* key, T value) {
  if (!ctx || !key) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  ctx->set<T>(key, std::move(value));
  return TF_OK;
}

TF_Error SetContextString(task_ctx* ctx, const char* key, const char* value) {
  if (!ctx || !key || !value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  ctx->set<std::string>(key, value);
  return TF_OK;
}

TF_Error ContextContains(const task_ctx* ctx, const char* key, int* out_contains) {
  if (!ctx || !key || !out_contains) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  *out_contains = ctx->contains(key) ? 1 : 0;
  return TF_OK;
}

template <typename T>
TF_Error GetContextValue(const task_ctx* ctx, const char* key, T* out_value) {
  if (!ctx || !key || !out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!ctx->contains(key)) {
    return TF_ERROR_VALUE_NOT_FOUND;
  }

  auto value = ctx->get<T>(key);
  if (!value) {
    return TF_ERROR_TYPE_MISMATCH;
  }

  *out_value = *value;
  return TF_OK;
}

TF_Error GetContextStringCopy(const task_ctx* ctx, const char* key, char** out_value, size_t* out_size) {
  if (!ctx || !key) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!ctx->contains(key)) {
    return TF_ERROR_VALUE_NOT_FOUND;
  }

  auto value = ctx->get<std::string>(key);
  if (!value) {
    return TF_ERROR_TYPE_MISMATCH;
  }

  return CopyStringToBuffer(*value, out_value, out_size);
}

bool HasResultStorage(const workflow_execution* execution) {
  return execution && execution->results().storage() != nullptr;
}

template <typename T>
TF_Error SetResultValue(workflow_execution* execution, uint64_t node_id, const char* key, T value) {
  if (!execution || !key) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!HasResultStorage(execution)) {
    return TF_ERROR_NOT_SUPPORTED;
  }

  execution->results().set(execution->id(), node_id, std::string(key), std::move(value));
  return TF_OK;
}

TF_Error SetResultString(workflow_execution* execution, uint64_t node_id, const char* key, const char* value) {
  if (!execution || !key || !value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!HasResultStorage(execution)) {
    return TF_ERROR_NOT_SUPPORTED;
  }

  execution->results().set(execution->id(), node_id, std::string(key), std::string(value));
  return TF_OK;
}

template <typename T>
TF_Error GetResultValue(const workflow_execution* execution, uint64_t from_node_id, const char* key, T* out_value) {
  if (!execution || !key || !out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!HasResultStorage(execution)) {
    return TF_ERROR_NOT_SUPPORTED;
  }

  const taskflow::core::result_locator loc{from_node_id, key};
  if (!execution->results().has(loc)) {
    return TF_ERROR_VALUE_NOT_FOUND;
  }

  auto value = execution->results().get<T>(loc);
  if (!value) {
    return TF_ERROR_TYPE_MISMATCH;
  }

  *out_value = *value;
  return TF_OK;
}

TF_Error GetResultStringCopy(const workflow_execution* execution, uint64_t from_node_id, const char* key,
                             char** out_value, size_t* out_size) {
  if (!execution || !key) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  if (!HasResultStorage(execution)) {
    return TF_ERROR_NOT_SUPPORTED;
  }

  const taskflow::core::result_locator loc{from_node_id, key};
  if (!execution->results().has(loc)) {
    return TF_ERROR_VALUE_NOT_FOUND;
  }

  auto value = execution->results().get<std::string>(loc);
  if (!value) {
    return TF_ERROR_TYPE_MISMATCH;
  }

  return CopyStringToBuffer(*value, out_value, out_size);
}

template <typename F>
TF_Error MutateBlueprintNode(BlueprintState* state, uint64_t node_id, F&& mutator) {
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  const auto* existing = state->blueprint.find_node(node_id);
  if (!existing) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto node = *existing;
  mutator(node);
  state->blueprint.add_node(std::move(node));
  return TF_OK;
}

TF_Error StoreExecutionHandle(const std::shared_ptr<OrchestratorState>& orchestrator_state, uint64_t execution_id,
                              const std::shared_ptr<AsyncExecutionState>& async_state, tf_execution_t* out_handle) {
  if (!out_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_handle = 0;
  const uint64_t handle = NextHandle();

  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (!orchestrator_state || orchestrator_state->destroyed) {
    return TF_ERROR_OPERATION_NOT_PERMITTED;
  }

  g_executions.emplace(handle, ExecutionState{orchestrator_state, execution_id, async_state});
  *out_handle = handle;
  return TF_OK;
}

void CacheExecutionResolution(const std::shared_ptr<AsyncExecutionState>& async_state, TF_Error error,
                              task_state state) {
  if (!async_state) {
    return;
  }
  std::lock_guard<std::mutex> lock(async_state->mutex);
  async_state->resolved = true;
  async_state->resolved_error = error;
  async_state->resolved_state = state;
}

bool AsyncExecutionRunning(const std::shared_ptr<AsyncExecutionState>& async_state) {
  if (!async_state) {
    return false;
  }

  std::shared_future<task_state> future;
  {
    std::lock_guard<std::mutex> lock(async_state->mutex);
    if (async_state->resolved || !async_state->has_future) {
      return false;
    }
    future = async_state->future;
  }

  return future.valid() && future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready;
}

TF_Error ResolveAsyncState(const std::shared_ptr<AsyncExecutionState>& async_state, bool block, int* out_is_ready,
                           int* out_state) {
  if (out_is_ready) {
    *out_is_ready = 0;
  }
  if (out_state) {
    *out_state = TF_TASK_STATE_PENDING;
  }
  if (!async_state) {
    return TF_OK;
  }

  std::shared_future<task_state> future;
  {
    std::lock_guard<std::mutex> lock(async_state->mutex);
    if (async_state->resolved) {
      if (out_is_ready) {
        *out_is_ready = 1;
      }
      if (out_state) {
        *out_state = ToTaskState(async_state->resolved_state);
      }
      return async_state->resolved_error;
    }
    if (!async_state->has_future) {
      return TF_OK;
    }
    future = async_state->future;
  }

  if (!future.valid()) {
    return TF_ERROR_INVALID_STATE;
  }
  if (!block && future.wait_for(std::chrono::milliseconds(0)) != std::future_status::ready) {
    return TF_OK;
  }

  TF_Error resolved_error = TF_OK;
  task_state resolved_state = task_state::pending;
  try {
    resolved_state = future.get();
  } catch (...) {
    resolved_error = MapCurrentException();
    resolved_state = task_state::failed;
  }

  {
    std::lock_guard<std::mutex> lock(async_state->mutex);
    async_state->resolved = true;
    async_state->resolved_error = resolved_error;
    async_state->resolved_state = resolved_state;
  }

  if (out_is_ready) {
    *out_is_ready = 1;
  }
  if (out_state) {
    *out_state = ToTaskState(resolved_state);
  }
  return resolved_error;
}

bool HasActiveExecutionsLocked(const OrchestratorState* orchestrator_state) {
  for (const auto& [handle, execution_state] : g_executions) {
    (void)handle;
    if (execution_state.orchestrator_state.get() != orchestrator_state) {
      continue;
    }

    if (AsyncExecutionRunning(execution_state.async_state)) {
      return true;
    }

    auto* execution = ResolveExecution(execution_state);
    if (execution && !execution->is_complete()) {
      return true;
    }
  }

  return false;
}

void PruneStaleExecutionHandlesLocked(const OrchestratorState* orchestrator_state) {
  for (auto it = g_executions.begin(); it != g_executions.end();) {
    if (it->second.orchestrator_state.get() == orchestrator_state && ResolveExecution(it->second) == nullptr) {
      it = g_executions.erase(it);
    } else {
      ++it;
    }
  }
}

int64_t ToEpochMillis(std::chrono::system_clock::time_point value) {
  if (value == std::chrono::system_clock::time_point{}) {
    return 0;
  }
  return std::chrono::duration_cast<std::chrono::milliseconds>(value.time_since_epoch()).count();
}

task_state InvokeTaskCallback(const std::shared_ptr<TaskCallbackRegistration>& registration,
                              const std::weak_ptr<OrchestratorState>& weak_orchestrator_state, task_ctx& ctx) noexcept {
  if (!registration || !registration->callback) {
    return task_state::failed;
  }

  workflow_execution* execution = nullptr;
  if (auto orchestrator_state = weak_orchestrator_state.lock()) {
    if (orchestrator_state->orchestrator) {
      execution = orchestrator_state->orchestrator->get_execution(ctx.exec_id());
    }
  }

  TaskContextView view;
  view.ctx = &ctx;
  view.execution = execution;
  view.node_id = ctx.node_id();

  try {
    return FromCallbackTaskState(
        registration->callback(reinterpret_cast<tf_task_context_t>(&view), registration->userdata), execution,
        view.node_id);
  } catch (...) {
    if (execution) {
      execution->set_node_error(view.node_id, "uncaught exception in C task callback");
    }
    return task_state::failed;
  }
}

bool InvokeEdgeCondition(const std::shared_ptr<EdgeConditionRegistration>& registration, const task_ctx& ctx) noexcept {
  if (!registration || !registration->callback) {
    return false;
  }

  TaskContextView view;
  view.ctx = const_cast<task_ctx*>(&ctx);
  view.execution = nullptr;
  view.node_id = 0;

  try {
    return registration->callback(reinterpret_cast<tf_task_context_t>(&view), registration->userdata) != 0;
  } catch (...) {
    return false;
  }
}

}  // namespace

extern "C" {

const char* taskflow_version_string(void) { return TASKFLOW_CAPI_VERSION_STRING; }

void tf_run_options_init(struct TF_RunOptions* options) {
  if (!options) {
    return;
  }

  options->struct_size = sizeof(TF_RunOptions);
  options->stop_on_first_failure = 1;
  options->compensate_on_failure = 0;
  options->compensate_on_cancel = 0;
}

void tf_orchestrator_options_init(struct TF_OrchestratorOptions* options) {
  if (!options) {
    return;
  }

  options->struct_size = sizeof(TF_OrchestratorOptions);
  options->enable_memory_state_storage = 0;
  options->enable_memory_result_storage = 0;
}

void tf_retry_policy_init(struct TF_RetryPolicy* policy) {
  if (!policy) {
    return;
  }

  retry_policy defaults{};
  policy->struct_size = sizeof(TF_RetryPolicy);
  policy->max_attempts = defaults.max_attempts;
  policy->initial_delay_ms = static_cast<uint64_t>(defaults.initial_delay.count());
  policy->backoff_multiplier = defaults.backoff_multiplier;
  policy->max_delay_ms = static_cast<uint64_t>(defaults.max_delay.count());
  policy->jitter = defaults.jitter ? 1 : 0;
  policy->jitter_range_ms = static_cast<uint64_t>(defaults.jitter_range.count());
}

int tf_blueprint_create(tf_blueprint_t* out_handle) {
  if (!out_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_handle = 0;

  try {
    auto state = std::make_shared<BlueprintState>();
    const uint64_t handle = NextHandle();
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_blueprints.emplace(handle, state);
    }
    *out_handle = handle;
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_destroy(tf_blueprint_t handle) {
  std::lock_guard<std::mutex> lock(g_state_mutex);
  if (g_blueprints.erase(handle) == 0) {
    return TF_ERROR_INVALID_HANDLE;
  }
  return TF_OK;
}

int tf_blueprint_from_json_copy(const char* json_content, tf_blueprint_t* out_handle) {
  if (!json_content || !out_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_handle = 0;

  try {
    auto blueprint = taskflow::workflow::serializer::from_json(json_content);
    if (!blueprint) {
      return TF_ERROR_INVALID_STATE;
    }

    auto state = std::make_shared<BlueprintState>();
    state->blueprint = std::move(*blueprint);

    const uint64_t handle = NextHandle();
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_blueprints.emplace(handle, state);
    }
    *out_handle = handle;
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_add_node(tf_blueprint_t handle, uint64_t node_id, const char* task_type, const char* label) {
  if (!task_type) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    taskflow::workflow::node_def node(node_id, task_type);
    if (label && *label != '\0') {
      node.label = std::string(label);
    }
    state->blueprint.add_node(std::move(node));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_set_node_retry(tf_blueprint_t handle, uint64_t node_id, const struct TF_RetryPolicy* retry) {
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  if (!retry) {
    return MutateBlueprintNode(state.get(), node_id, [](taskflow::workflow::node_def& node) { node.retry.reset(); });
  }

  retry_policy normalized{};
  if (!NormalizeRetryPolicy(retry, &normalized)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  return MutateBlueprintNode(state.get(), node_id,
                             [&](taskflow::workflow::node_def& node) { node.retry = normalized; });
}

int tf_blueprint_set_node_compensation(tf_blueprint_t handle, uint64_t node_id, const char* compensate_task_type,
                                       const struct TF_RetryPolicy* compensate_retry) {
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  std::optional<retry_policy> normalized_retry;
  if (compensate_retry) {
    retry_policy retry_value{};
    if (!NormalizeRetryPolicy(compensate_retry, &retry_value)) {
      return TF_ERROR_INVALID_ARGUMENT;
    }
    normalized_retry = retry_value;
  }

  return MutateBlueprintNode(state.get(), node_id, [&](taskflow::workflow::node_def& node) {
    if (!compensate_task_type || *compensate_task_type == '\0') {
      node.compensate_task_type.reset();
      node.compensate_retry.reset();
      return;
    }

    node.compensate_task_type = std::string(compensate_task_type);
    node.compensate_retry = normalized_retry;
  });
}

int tf_blueprint_add_node_tag(tf_blueprint_t handle, uint64_t node_id, const char* tag) {
  if (!tag) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  return MutateBlueprintNode(state.get(), node_id,
                             [&](taskflow::workflow::node_def& node) { node.tags.emplace_back(tag); });
}

int tf_blueprint_add_edge(tf_blueprint_t handle, uint64_t from_node_id, uint64_t to_node_id) {
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    state->blueprint.add_edge(taskflow::workflow::edge_def(from_node_id, to_node_id));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_add_conditional_edge(tf_blueprint_t handle, uint64_t from_node_id, uint64_t to_node_id,
                                      tf_edge_condition_callback_fn callback, void* userdata) {
  if (!callback) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    auto registration = std::make_shared<EdgeConditionRegistration>();
    registration->callback = callback;
    registration->userdata = userdata;

    taskflow::workflow::edge_def edge(from_node_id, to_node_id);
    edge.condition = [registration](const task_ctx& ctx) { return InvokeEdgeCondition(registration, ctx); };
    state->blueprint.add_edge(std::move(edge));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_has_node(tf_blueprint_t handle, uint64_t node_id, int* out_has_node) {
  if (!out_has_node) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_has_node = 0;
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_has_node = state->blueprint.has_node(node_id) ? 1 : 0;
  return TF_OK;
}

int tf_blueprint_get_node_count(tf_blueprint_t handle, size_t* out_count) {
  if (!out_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_count = 0;
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_count = state->blueprint.nodes().size();
  return TF_OK;
}

int tf_blueprint_get_edge_count(tf_blueprint_t handle, size_t* out_count) {
  if (!out_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_count = 0;
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_count = state->blueprint.edges().size();
  return TF_OK;
}

int tf_blueprint_is_valid(tf_blueprint_t handle, int* out_is_valid) {
  if (!out_is_valid) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_is_valid = 0;
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_is_valid = state->blueprint.is_valid() ? 1 : 0;
  return TF_OK;
}

int tf_blueprint_validate_copy(tf_blueprint_t handle, char** out_message, size_t* out_size) {
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    return CopyStringToBuffer(state->blueprint.validate(), out_message, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_blueprint_to_json_copy(tf_blueprint_t handle, char** out_json, size_t* out_size) {
  auto state = FindBlueprint(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    return CopyStringToBuffer(taskflow::workflow::serializer::to_json(state->blueprint), out_json, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_create(tf_orchestrator_t* out_handle) { return tf_orchestrator_create_ex(nullptr, out_handle); }

int tf_orchestrator_create_ex(const struct TF_OrchestratorOptions* options, tf_orchestrator_t* out_handle) {
  if (!out_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_handle = 0;

  bool enable_state_storage = false;
  bool enable_result_storage = false;
  if (!NormalizeOrchestratorOptions(options, &enable_state_storage, &enable_result_storage)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  try {
    auto state = std::make_shared<OrchestratorState>();
    state->callback_state = std::make_shared<CallbackState>();

    if (enable_state_storage && enable_result_storage) {
      state->orchestrator = std::make_unique<taskflow::engine::orchestrator>(
          std::make_unique<taskflow::engine::default_thread_pool>(),
          std::make_unique<taskflow::storage::memory_state_storage>(),
          std::make_unique<taskflow::storage::memory_result_storage>());
    } else if (enable_state_storage) {
      state->orchestrator =
          std::make_unique<taskflow::engine::orchestrator>(std::make_unique<taskflow::storage::memory_state_storage>());
    } else if (enable_result_storage) {
      state->orchestrator = std::make_unique<taskflow::engine::orchestrator>(
          std::make_unique<taskflow::storage::memory_result_storage>());
    } else {
      state->orchestrator = std::make_unique<taskflow::engine::orchestrator>();
    }

    state->observer = std::make_unique<c_api_observer>(state->callback_state);
    state->orchestrator->add_observer(state->observer.get());

    const uint64_t handle = NextHandle();
    {
      std::lock_guard<std::mutex> lock(g_state_mutex);
      g_orchestrators.emplace(handle, state);
    }

    *out_handle = handle;
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_destroy(tf_orchestrator_t handle) {
  std::shared_ptr<OrchestratorState> removed_state;

  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    auto it = g_orchestrators.find(handle);
    if (it == g_orchestrators.end() || it->second->destroyed) {
      return TF_ERROR_INVALID_HANDLE;
    }

    if (HasActiveExecutionsLocked(it->second.get())) {
      return TF_ERROR_OPERATION_NOT_PERMITTED;
    }

    removed_state = it->second;
    removed_state->destroyed = true;
    g_orchestrators.erase(it);

    for (auto exec_it = g_executions.begin(); exec_it != g_executions.end();) {
      if (exec_it->second.orchestrator_state.get() == removed_state.get()) {
        exec_it = g_executions.erase(exec_it);
      } else {
        ++exec_it;
      }
    }
  }

  return TF_OK;
}

int tf_orchestrator_register_task_callback(tf_orchestrator_t handle, const char* task_type,
                                           tf_task_callback_fn callback, void* userdata) {
  if (!task_type || !callback) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    auto registration = std::make_shared<TaskCallbackRegistration>();
    registration->callback = callback;
    registration->userdata = userdata;
    const std::weak_ptr<OrchestratorState> weak_state = state;

    state->orchestrator->register_task(task_type, [registration, weak_state]() {
      return taskflow::core::task_wrapper{
          [registration, weak_state](task_ctx& ctx) { return InvokeTaskCallback(registration, weak_state, ctx); }};
    });
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_has_task(tf_orchestrator_t handle, const char* task_type, int* out_has_task) {
  if (!task_type || !out_has_task) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_has_task = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_has_task = state->orchestrator->registry().has_task(task_type) ? 1 : 0;
  return TF_OK;
}

int tf_orchestrator_register_blueprint(tf_orchestrator_t handle, uint64_t blueprint_id,
                                       tf_blueprint_t blueprint_handle) {
  auto orchestrator_state = FindOrchestrator(handle);
  if (!orchestrator_state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  auto blueprint_state = FindBlueprint(blueprint_handle);
  if (!blueprint_state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    orchestrator_state->orchestrator->register_blueprint(blueprint_id, CloneBlueprint(blueprint_state->blueprint));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_register_blueprint_name(tf_orchestrator_t handle, const char* blueprint_name,
                                            tf_blueprint_t blueprint_handle) {
  if (!blueprint_name) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto orchestrator_state = FindOrchestrator(handle);
  if (!orchestrator_state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  auto blueprint_state = FindBlueprint(blueprint_handle);
  if (!blueprint_state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    orchestrator_state->orchestrator->register_blueprint(blueprint_name, CloneBlueprint(blueprint_state->blueprint));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_register_blueprint_json(tf_orchestrator_t handle, uint64_t blueprint_id, const char* json_content) {
  if (!json_content) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    auto blueprint = taskflow::workflow::serializer::from_json(json_content);
    if (!blueprint) {
      return TF_ERROR_INVALID_STATE;
    }

    state->orchestrator->register_blueprint(blueprint_id, std::move(*blueprint));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_register_blueprint_name_json(tf_orchestrator_t handle, const char* blueprint_name,
                                                 const char* json_content) {
  if (!blueprint_name || !json_content) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    auto blueprint = taskflow::workflow::serializer::from_json(json_content);
    if (!blueprint) {
      return TF_ERROR_INVALID_STATE;
    }

    state->orchestrator->register_blueprint(blueprint_name, std::move(*blueprint));
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_has_blueprint(tf_orchestrator_t handle, uint64_t blueprint_id, int* out_has_blueprint) {
  if (!out_has_blueprint) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_has_blueprint = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_has_blueprint = state->orchestrator->has_blueprint(blueprint_id) ? 1 : 0;
  return TF_OK;
}

int tf_orchestrator_has_blueprint_name(tf_orchestrator_t handle, const char* blueprint_name, int* out_has_blueprint) {
  if (!blueprint_name || !out_has_blueprint) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_has_blueprint = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  *out_has_blueprint = state->orchestrator->has_blueprint(blueprint_name) ? 1 : 0;
  return TF_OK;
}

int tf_orchestrator_get_blueprint_json_copy(tf_orchestrator_t handle, uint64_t blueprint_id, char** out_json,
                                            size_t* out_size) {
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    const auto* blueprint = state->orchestrator->get_blueprint(blueprint_id);
    if (!blueprint) {
      return TF_ERROR_BLUEPRINT_NOT_FOUND;
    }
    return CopyStringToBuffer(taskflow::workflow::serializer::to_json(*blueprint), out_json, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_get_blueprint_name_json_copy(tf_orchestrator_t handle, const char* blueprint_name, char** out_json,
                                                 size_t* out_size) {
  if (!blueprint_name) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    const auto* blueprint = state->orchestrator->get_blueprint(blueprint_name);
    if (!blueprint) {
      return TF_ERROR_BLUEPRINT_NOT_FOUND;
    }
    return CopyStringToBuffer(taskflow::workflow::serializer::to_json(*blueprint), out_json, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_set_event_callback(tf_orchestrator_t handle, tf_event_callback_fn callback, void* userdata) {
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  std::lock_guard<std::mutex> lock(state->callback_state->mutex);
  state->callback_state->callback = callback;
  state->callback_state->userdata = userdata;
  return TF_OK;
}

int tf_orchestrator_create_execution(tf_orchestrator_t handle, uint64_t blueprint_id,
                                     tf_execution_t* out_execution_handle) {
  if (!out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    const auto execution_id = state->orchestrator->create_execution(blueprint_id);
    auto async_state = std::make_shared<AsyncExecutionState>();
    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_create_execution_name(tf_orchestrator_t handle, const char* blueprint_name,
                                          tf_execution_t* out_execution_handle) {
  if (!blueprint_name || !out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    const auto execution_id = state->orchestrator->create_execution(blueprint_name);
    auto async_state = std::make_shared<AsyncExecutionState>();
    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_run_sync(tf_orchestrator_t handle, uint64_t blueprint_id, const struct TF_RunOptions* opts,
                             tf_execution_t* out_execution_handle) {
  if (!out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  orchestrator_run_options run_options;
  if (!NormalizeRunOptions(opts, &run_options)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  uint64_t execution_id = 0;
  try {
    execution_id = state->orchestrator->create_execution(blueprint_id);
    const auto final_state = state->orchestrator->run_sync(execution_id, run_options);
    auto async_state = std::make_shared<AsyncExecutionState>();
    CacheExecutionResolution(async_state, TF_OK, final_state);

    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    if (execution_id != 0) {
      state->orchestrator->cleanup_execution(execution_id);
    }
    return MapCurrentException();
  }
}

int tf_orchestrator_run_sync_name(tf_orchestrator_t handle, const char* blueprint_name,
                                  const struct TF_RunOptions* opts, tf_execution_t* out_execution_handle) {
  if (!blueprint_name || !out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  orchestrator_run_options run_options;
  if (!NormalizeRunOptions(opts, &run_options)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  uint64_t execution_id = 0;
  try {
    execution_id = state->orchestrator->create_execution(blueprint_name);
    const auto final_state = state->orchestrator->run_sync(execution_id, run_options);
    auto async_state = std::make_shared<AsyncExecutionState>();
    CacheExecutionResolution(async_state, TF_OK, final_state);

    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    if (execution_id != 0) {
      state->orchestrator->cleanup_execution(execution_id);
    }
    return MapCurrentException();
  }
}

int tf_orchestrator_run_async(tf_orchestrator_t handle, uint64_t blueprint_id, const struct TF_RunOptions* opts,
                              tf_execution_t* out_execution_handle) {
  if (!out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  orchestrator_run_options run_options;
  if (!NormalizeRunOptions(opts, &run_options)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  uint64_t execution_id = 0;
  try {
    execution_id = state->orchestrator->create_execution(blueprint_id);
    auto future = state->orchestrator->run_async(execution_id, run_options).share();
    auto async_state = std::make_shared<AsyncExecutionState>();
    {
      std::lock_guard<std::mutex> lock(async_state->mutex);
      async_state->has_future = true;
      async_state->future = std::move(future);
    }

    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    if (execution_id != 0) {
      state->orchestrator->cleanup_execution(execution_id);
    }
    return MapCurrentException();
  }
}

int tf_orchestrator_run_async_name(tf_orchestrator_t handle, const char* blueprint_name,
                                   const struct TF_RunOptions* opts, tf_execution_t* out_execution_handle) {
  if (!blueprint_name || !out_execution_handle) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_handle = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  orchestrator_run_options run_options;
  if (!NormalizeRunOptions(opts, &run_options)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  uint64_t execution_id = 0;
  try {
    execution_id = state->orchestrator->create_execution(blueprint_name);
    auto future = state->orchestrator->run_async(execution_id, run_options).share();
    auto async_state = std::make_shared<AsyncExecutionState>();
    {
      std::lock_guard<std::mutex> lock(async_state->mutex);
      async_state->has_future = true;
      async_state->future = std::move(future);
    }

    const TF_Error store_rc = StoreExecutionHandle(state, execution_id, async_state, out_execution_handle);
    if (store_rc != TF_OK) {
      state->orchestrator->cleanup_execution(execution_id);
      return store_rc;
    }
    return TF_OK;
  } catch (...) {
    if (execution_id != 0) {
      state->orchestrator->cleanup_execution(execution_id);
    }
    return MapCurrentException();
  }
}

int tf_orchestrator_cleanup_completed_executions(tf_orchestrator_t handle, size_t* out_count) {
  if (!out_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_count = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    *out_count = state->orchestrator->cleanup_completed_executions();
    std::lock_guard<std::mutex> lock(g_state_mutex);
    PruneStaleExecutionHandlesLocked(state.get());
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_orchestrator_cleanup_old_executions(tf_orchestrator_t handle, uint64_t older_than_ms, size_t* out_count) {
  if (!out_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_count = 0;
  auto state = FindOrchestrator(handle);
  if (!state) {
    return TF_ERROR_INVALID_HANDLE;
  }

  try {
    *out_count = state->orchestrator->cleanup_old_executions(std::chrono::milliseconds(older_than_ms));
    std::lock_guard<std::mutex> lock(g_state_mutex);
    PruneStaleExecutionHandlesLocked(state.get());
    return TF_OK;
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_execution_run_sync(tf_execution_t handle, const struct TF_RunOptions* opts, int* out_state) {
  if (!out_state) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_state = TF_TASK_STATE_PENDING;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  if (AsyncExecutionRunning(execution_state.async_state)) {
    return TF_ERROR_OPERATION_NOT_PERMITTED;
  }

  auto* orchestrator = execution_state.orchestrator_state->orchestrator.get();
  if (!orchestrator) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  orchestrator_run_options run_options;
  if (!NormalizeRunOptions(opts, &run_options)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  try {
    const auto final_state = orchestrator->run_sync(execution_state.execution_id, run_options);
    CacheExecutionResolution(execution_state.async_state, TF_OK, final_state);
    *out_state = ToTaskState(final_state);
    return TF_OK;
  } catch (...) {
    const TF_Error error = MapCurrentException();
    CacheExecutionResolution(execution_state.async_state, error, task_state::failed);
    *out_state = TF_TASK_STATE_FAILED;
    return error;
  }
}

int tf_execution_wait(tf_execution_t handle, int* out_state) {
  if (!out_state) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_state = TF_TASK_STATE_PENDING;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  int is_ready = 0;
  TF_Error rc = ResolveAsyncState(execution_state.async_state, true, &is_ready, out_state);
  if (is_ready) {
    return rc;
  }

  if (execution->is_complete()) {
    const auto final_state = execution->overall_state();
    CacheExecutionResolution(execution_state.async_state, TF_OK, final_state);
    *out_state = ToTaskState(final_state);
    return TF_OK;
  }

  return TF_ERROR_INVALID_STATE;
}

int tf_execution_poll(tf_execution_t handle, int* out_is_ready, int* out_state) {
  if (!out_is_ready) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_is_ready = 0;
  if (out_state) {
    *out_state = TF_TASK_STATE_PENDING;
  }

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  TF_Error rc = ResolveAsyncState(execution_state.async_state, false, out_is_ready, out_state);
  if (*out_is_ready != 0) {
    return rc;
  }

  if (execution->is_complete()) {
    const auto final_state = execution->overall_state();
    CacheExecutionResolution(execution_state.async_state, TF_OK, final_state);
    *out_is_ready = 1;
    if (out_state) {
      *out_state = ToTaskState(final_state);
    }
  }
  return TF_OK;
}

int tf_execution_cancel(tf_execution_t handle) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* orchestrator = execution_state.orchestrator_state->orchestrator.get();
  if (!orchestrator) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  return orchestrator->cancel_execution(execution_state.execution_id) ? TF_OK : TF_ERROR_EXECUTION_NOT_FOUND;
}

int tf_execution_get_id(tf_execution_t handle, uint64_t* out_execution_id) {
  if (!out_execution_id) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_execution_id = 0;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_execution_id = execution_state.execution_id;
  return TF_OK;
}

int tf_execution_snapshot_json_copy(tf_execution_t handle, char** out_json, size_t* out_size) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  try {
    return CopyStringToBuffer(execution->to_snapshot_json(), out_json, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_execution_get_overall_state(tf_execution_t handle, int* out_state) {
  if (!out_state) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_state = TF_TASK_STATE_PENDING;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_state = ToTaskState(execution->overall_state());
  return TF_OK;
}

int tf_execution_get_node_state(tf_execution_t handle, uint64_t node_id, int* out_state) {
  if (!out_state) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_state = TF_TASK_STATE_PENDING;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  const auto* blueprint = execution->blueprint();
  if (!blueprint || !blueprint->has_node(node_id)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_state = ToTaskState(execution->get_node_state(node_id).state);
  return TF_OK;
}

int tf_execution_get_node_retry_count(tf_execution_t handle, uint64_t node_id, int32_t* out_retry_count) {
  if (!out_retry_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_retry_count = 0;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  const auto* blueprint = execution->blueprint();
  if (!blueprint || !blueprint->has_node(node_id)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_retry_count = execution->retry_count(node_id);
  return TF_OK;
}

int tf_execution_get_node_error_copy(tf_execution_t handle, uint64_t node_id, char** out_error, size_t* out_size) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  const auto* blueprint = execution->blueprint();
  if (!blueprint || !blueprint->has_node(node_id)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  try {
    return CopyStringToBuffer(execution->get_node_state(node_id).error_message, out_error, out_size);
  } catch (...) {
    return MapCurrentException();
  }
}

int tf_execution_count_by_state(tf_execution_t handle, int task_state_value, size_t* out_count) {
  if (!out_count) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_count = 0;

  task_state parsed_state = task_state::pending;
  if (!ParseTaskState(task_state_value, &parsed_state)) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_count = execution->count_by_state(parsed_state);
  return TF_OK;
}

int tf_execution_is_complete(tf_execution_t handle, int* out_is_complete) {
  if (!out_is_complete) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_is_complete = 0;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_is_complete = execution->is_complete() ? 1 : 0;
  return TF_OK;
}

int tf_execution_is_cancelled(tf_execution_t handle, int* out_is_cancelled) {
  if (!out_is_cancelled) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_is_cancelled = 0;

  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_is_cancelled = execution->is_cancelled() ? 1 : 0;
  return TF_OK;
}

int tf_execution_get_start_time_epoch_ms(tf_execution_t handle, int64_t* out_epoch_ms) {
  if (!out_epoch_ms) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_epoch_ms = 0;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_epoch_ms = ToEpochMillis(execution->start_time());
  return TF_OK;
}

int tf_execution_get_end_time_epoch_ms(tf_execution_t handle, int64_t* out_epoch_ms) {
  if (!out_epoch_ms) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  *out_epoch_ms = 0;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  *out_epoch_ms = ToEpochMillis(execution->end_time());
  return TF_OK;
}

int tf_execution_context_contains(tf_execution_t handle, const char* key, int* out_contains) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return ContextContains(&execution->context(), key, out_contains);
}

int tf_execution_context_set_bool(tf_execution_t handle, const char* key, int value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return SetContextValue<bool>(&execution->context(), key, value != 0);
}

int tf_execution_context_get_bool(tf_execution_t handle, const char* key, int* out_value) {
  if (!out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  bool value = false;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  const int rc = GetContextValue<bool>(&execution->context(), key, &value);
  if (rc == TF_OK) {
    *out_value = value ? 1 : 0;
  }
  return rc;
}

int tf_execution_context_set_int64(tf_execution_t handle, const char* key, int64_t value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return SetContextValue<int64_t>(&execution->context(), key, value);
}

int tf_execution_context_get_int64(tf_execution_t handle, const char* key, int64_t* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetContextValue<int64_t>(&execution->context(), key, out_value);
}

int tf_execution_context_set_uint64(tf_execution_t handle, const char* key, uint64_t value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return SetContextValue<uint64_t>(&execution->context(), key, value);
}

int tf_execution_context_get_uint64(tf_execution_t handle, const char* key, uint64_t* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetContextValue<uint64_t>(&execution->context(), key, out_value);
}

int tf_execution_context_set_double(tf_execution_t handle, const char* key, double value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return SetContextValue<double>(&execution->context(), key, value);
}

int tf_execution_context_get_double(tf_execution_t handle, const char* key, double* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetContextValue<double>(&execution->context(), key, out_value);
}

int tf_execution_context_set_string(tf_execution_t handle, const char* key, const char* value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return SetContextString(&execution->context(), key, value);
}

int tf_execution_context_get_string_copy(tf_execution_t handle, const char* key, char** out_value, size_t* out_size) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetContextStringCopy(&execution->context(), key, out_value, out_size);
}

int tf_execution_result_get_bool(tf_execution_t handle, uint64_t from_node_id, const char* key, int* out_value) {
  if (!out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  bool value = false;
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  const int rc = GetResultValue<bool>(execution, from_node_id, key, &value);
  if (rc == TF_OK) {
    *out_value = value ? 1 : 0;
  }
  return rc;
}

int tf_execution_result_get_int64(tf_execution_t handle, uint64_t from_node_id, const char* key, int64_t* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetResultValue<int64_t>(execution, from_node_id, key, out_value);
}

int tf_execution_result_get_uint64(tf_execution_t handle, uint64_t from_node_id, const char* key, uint64_t* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetResultValue<uint64_t>(execution, from_node_id, key, out_value);
}

int tf_execution_result_get_double(tf_execution_t handle, uint64_t from_node_id, const char* key, double* out_value) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetResultValue<double>(execution, from_node_id, key, out_value);
}

int tf_execution_result_get_string_copy(tf_execution_t handle, uint64_t from_node_id, const char* key, char** out_value,
                                        size_t* out_size) {
  ExecutionState execution_state;
  if (!FindExecution(handle, &execution_state)) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  auto* execution = ResolveExecution(execution_state);
  if (!execution) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }
  return GetResultStringCopy(execution, from_node_id, key, out_value, out_size);
}

int tf_execution_destroy(tf_execution_t handle) {
  ExecutionState execution_state;
  {
    std::lock_guard<std::mutex> lock(g_state_mutex);
    auto it = g_executions.find(handle);
    if (it == g_executions.end()) {
      return TF_ERROR_EXECUTION_NOT_FOUND;
    }
    execution_state = it->second;
    if (AsyncExecutionRunning(execution_state.async_state)) {
      return TF_ERROR_OPERATION_NOT_PERMITTED;
    }
    auto* execution = ResolveExecution(execution_state);
    if (execution && !execution->is_complete()) {
      return TF_ERROR_OPERATION_NOT_PERMITTED;
    }
    g_executions.erase(it);
  }

  auto* orchestrator = execution_state.orchestrator_state->orchestrator.get();
  if (!orchestrator) {
    return TF_ERROR_EXECUTION_NOT_FOUND;
  }

  return orchestrator->cleanup_execution(execution_state.execution_id) ? TF_OK : TF_ERROR_EXECUTION_NOT_FOUND;
}

int tf_task_context_get_exec_id(tf_task_context_t ctx, uint64_t* out_exec_id) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_exec_id) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  *out_exec_id = view->ctx->exec_id();
  return TF_OK;
}

int tf_task_context_get_node_id(tf_task_context_t ctx, uint64_t* out_node_id) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_node_id) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  *out_node_id = view->node_id;
  return TF_OK;
}

int tf_task_context_report_progress(tf_task_context_t ctx, double progress) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  view->ctx->report_progress(static_cast<float>(progress));
  return TF_OK;
}

int tf_task_context_get_progress(tf_task_context_t ctx, double* out_progress) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_progress) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  *out_progress = view->ctx->progress();
  return TF_OK;
}

int tf_task_context_is_cancelled(tf_task_context_t ctx, int* out_is_cancelled) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_is_cancelled) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  const bool cancelled = view->ctx->is_cancelled() || (view->execution && view->execution->is_cancelled());
  *out_is_cancelled = cancelled ? 1 : 0;
  return TF_OK;
}

int tf_task_context_request_cancel(tf_task_context_t ctx) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  view->ctx->cancel();
  if (view->execution) {
    view->execution->cancel();
  }
  return TF_OK;
}

int tf_task_context_set_error(tf_task_context_t ctx, const char* error_message) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->execution || !error_message) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  view->execution->set_node_error(view->node_id, error_message);
  return TF_OK;
}

int tf_task_context_contains(tf_task_context_t ctx, const char* key, int* out_contains) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return ContextContains(view->ctx, key, out_contains);
}

int tf_task_context_set_bool(tf_task_context_t ctx, const char* key, int value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetContextValue<bool>(view->ctx, key, value != 0);
}

int tf_task_context_get_bool(tf_task_context_t ctx, const char* key, int* out_value) {
  if (!out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  bool value = false;
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  const int rc = GetContextValue<bool>(view->ctx, key, &value);
  if (rc == TF_OK) {
    *out_value = value ? 1 : 0;
  }
  return rc;
}

int tf_task_context_set_int64(tf_task_context_t ctx, const char* key, int64_t value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetContextValue<int64_t>(view->ctx, key, value);
}

int tf_task_context_get_int64(tf_task_context_t ctx, const char* key, int64_t* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetContextValue<int64_t>(view->ctx, key, out_value);
}

int tf_task_context_set_uint64(tf_task_context_t ctx, const char* key, uint64_t value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetContextValue<uint64_t>(view->ctx, key, value);
}

int tf_task_context_get_uint64(tf_task_context_t ctx, const char* key, uint64_t* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetContextValue<uint64_t>(view->ctx, key, out_value);
}

int tf_task_context_set_double(tf_task_context_t ctx, const char* key, double value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetContextValue<double>(view->ctx, key, value);
}

int tf_task_context_get_double(tf_task_context_t ctx, const char* key, double* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetContextValue<double>(view->ctx, key, out_value);
}

int tf_task_context_set_string(tf_task_context_t ctx, const char* key, const char* value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetContextString(view->ctx, key, value);
}

int tf_task_context_get_string_copy(tf_task_context_t ctx, const char* key, char** out_value, size_t* out_size) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetContextStringCopy(view->ctx, key, out_value, out_size);
}

int tf_task_context_set_result_bool(tf_task_context_t ctx, const char* key, int value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !view->execution) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetResultValue<bool>(view->execution, view->node_id, key, value != 0);
}

int tf_task_context_get_result_bool(tf_task_context_t ctx, uint64_t from_node_id, const char* key, int* out_value) {
  if (!out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }

  bool value = false;
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !out_value) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  const int rc = GetResultValue<bool>(view->execution, from_node_id, key, &value);
  if (rc == TF_OK) {
    *out_value = value ? 1 : 0;
  }
  return rc;
}

int tf_task_context_set_result_int64(tf_task_context_t ctx, const char* key, int64_t value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !view->execution) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetResultValue<int64_t>(view->execution, view->node_id, key, value);
}

int tf_task_context_get_result_int64(tf_task_context_t ctx, uint64_t from_node_id, const char* key,
                                     int64_t* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetResultValue<int64_t>(view->execution, from_node_id, key, out_value);
}

int tf_task_context_set_result_uint64(tf_task_context_t ctx, const char* key, uint64_t value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !view->execution) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetResultValue<uint64_t>(view->execution, view->node_id, key, value);
}

int tf_task_context_get_result_uint64(tf_task_context_t ctx, uint64_t from_node_id, const char* key,
                                      uint64_t* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetResultValue<uint64_t>(view->execution, from_node_id, key, out_value);
}

int tf_task_context_set_result_double(tf_task_context_t ctx, const char* key, double value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !view->execution) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetResultValue<double>(view->execution, view->node_id, key, value);
}

int tf_task_context_get_result_double(tf_task_context_t ctx, uint64_t from_node_id, const char* key,
                                      double* out_value) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetResultValue<double>(view->execution, from_node_id, key, out_value);
}

int tf_task_context_set_result_string(tf_task_context_t ctx, const char* key, const char* value) {
  auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx || !view->execution) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return SetResultString(view->execution, view->node_id, key, value);
}

int tf_task_context_get_result_string_copy(tf_task_context_t ctx, uint64_t from_node_id, const char* key,
                                           char** out_value, size_t* out_size) {
  const auto* view = ToTaskContextView(ctx);
  if (!view || !view->ctx) {
    return TF_ERROR_INVALID_ARGUMENT;
  }
  return GetResultStringCopy(view->execution, from_node_id, key, out_value, out_size);
}

void tf_string_free(char* value) { std::free(value); }

const char* tf_error_message(int error_code) {
  switch (error_code) {
    case TF_OK:
      return "Success";
    case TF_ERROR_UNKNOWN:
      return "Unknown error";
    case TF_ERROR_INVALID_HANDLE:
      return "Invalid handle";
    case TF_ERROR_BLUEPRINT_NOT_FOUND:
      return "Blueprint not found";
    case TF_ERROR_EXECUTION_NOT_FOUND:
      return "Execution not found";
    case TF_ERROR_BUS_SHUTDOWN:
      return "Bus shutdown";
    case TF_ERROR_INVALID_STATE:
      return "Invalid state";
    case TF_ERROR_INVALID_ARGUMENT:
      return "Invalid argument";
    case TF_ERROR_TASK_TYPE_NOT_FOUND:
      return "Task type not found";
    case TF_ERROR_OPERATION_NOT_PERMITTED:
      return "Operation not permitted";
    case TF_ERROR_STORAGE:
      return "Storage error";
    case TF_ERROR_TYPE_MISMATCH:
      return "Type mismatch";
    case TF_ERROR_VALUE_NOT_FOUND:
      return "Value not found";
    case TF_ERROR_NOT_SUPPORTED:
      return "Operation not supported";
    default:
      return "Unknown error code";
  }
}

const char* tf_task_state_name(int task_state_value) {
  switch (task_state_value) {
    case TF_TASK_STATE_PENDING:
      return "pending";
    case TF_TASK_STATE_RUNNING:
      return "running";
    case TF_TASK_STATE_SUCCESS:
      return "success";
    case TF_TASK_STATE_FAILED:
      return "failed";
    case TF_TASK_STATE_RETRY:
      return "retry";
    case TF_TASK_STATE_SKIPPED:
      return "skipped";
    case TF_TASK_STATE_CANCELLED:
      return "cancelled";
    case TF_TASK_STATE_COMPENSATING:
      return "compensating";
    case TF_TASK_STATE_COMPENSATED:
      return "compensated";
    case TF_TASK_STATE_COMPENSATION_FAILED:
      return "compensation_failed";
    default:
      return "unknown";
  }
}

}  // extern "C"
