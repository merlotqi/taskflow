#include <pybind11/pybind11.h>
#include <taskflow_c.h>

#include <atomic>
#include <cmath>
#include <cstdint>
#include <limits>
#include <memory>
#include <stdexcept>
#include <string>
#include <utility>
#include <vector>

namespace py = pybind11;

namespace {

struct GenericValueReader {
  std::function<int(char**, size_t*)> get_string;
  std::function<int(int*)> get_bool;
  std::function<int(uint64_t*)> get_uint64;
  std::function<int(int64_t*)> get_int64;
  std::function<int(double*)> get_double;
};

struct GenericValueWriter {
  std::function<int(int)> set_bool;
  std::function<int(int64_t)> set_int64;
  std::function<int(uint64_t)> set_uint64;
  std::function<int(double)> set_double;
  std::function<int(const char*)> set_string;
};

struct PyTaskRegistration {
  explicit PyTaskRegistration(py::function fn) : function(std::move(fn)) {}
  py::function function;
};

struct PyConditionRegistration {
  explicit PyConditionRegistration(py::function fn) : function(std::move(fn)) {}
  py::function function;
};

bool IsLookupMiss(int rc) { return rc == TF_ERROR_VALUE_NOT_FOUND || rc == TF_ERROR_TYPE_MISMATCH; }

std::string TakeOwnedString(char* buffer, size_t size) {
  std::string value = buffer ? std::string(buffer, size) : std::string{};
  tf_string_free(buffer);
  return value;
}

[[noreturn]] void ThrowTaskflowError(int rc, const char* action) {
  const std::string prefix = action ? std::string(action) + ": " : std::string{};
  throw std::runtime_error(prefix + tf_error_message(rc));
}

void EnsureTaskflowOk(int rc, const char* action) {
  if (rc != TF_OK) {
    ThrowTaskflowError(rc, action);
  }
}

py::object GetDictValue(const py::dict& dict, const char* key) {
  py::str lookup(key);
  if (dict.contains(lookup)) {
    return py::reinterpret_borrow<py::object>(dict[lookup]);
  }
  return py::none();
}

py::object GetDictValueAny(const py::dict& dict, std::initializer_list<const char*> keys) {
  for (const char* key : keys) {
    py::object value = GetDictValue(dict, key);
    if (!value.is_none()) {
      return value;
    }
  }
  return py::none();
}

std::string ReadString(const py::handle& value, const char* name) {
  if (!py::isinstance<py::str>(value)) {
    throw py::type_error(std::string(name) + " must be a string");
  }
  return py::cast<std::string>(value);
}

uint64_t ReadUint64(const py::handle& value, const char* name) {
  if (py::isinstance<py::bool_>(value) || !py::isinstance<py::int_>(value)) {
    throw py::type_error(std::string(name) + " must be an integer");
  }

  try {
    return py::cast<uint64_t>(value);
  } catch (const py::cast_error&) {
    throw py::type_error(std::string(name) + " must fit into an unsigned 64-bit integer");
  }
}

bool ReadBoolFromDict(const py::dict& dict, std::initializer_list<const char*> keys, bool default_value) {
  py::object value = GetDictValueAny(dict, keys);
  if (value.is_none()) {
    return default_value;
  }
  return py::cast<bool>(value);
}

TF_RunOptions ParseRunOptions(const py::object& value) {
  TF_RunOptions options{};
  tf_run_options_init(&options);

  if (value.is_none()) {
    return options;
  }
  if (!py::isinstance<py::dict>(value)) {
    throw py::type_error("run options must be a dict when provided");
  }

  py::dict dict = value.cast<py::dict>();
  options.stop_on_first_failure =
      ReadBoolFromDict(dict, {"stop_on_first_failure", "stopOnFirstFailure"}, options.stop_on_first_failure != 0) ? 1
                                                                                                                    : 0;
  options.compensate_on_failure =
      ReadBoolFromDict(dict, {"compensate_on_failure", "compensateOnFailure"}, options.compensate_on_failure != 0) ? 1
                                                                                                                      : 0;
  options.compensate_on_cancel =
      ReadBoolFromDict(dict, {"compensate_on_cancel", "compensateOnCancel"}, options.compensate_on_cancel != 0) ? 1
                                                                                                                    : 0;
  return options;
}

TF_OrchestratorOptions ParseOrchestratorOptions(const py::object& value) {
  TF_OrchestratorOptions options{};
  tf_orchestrator_options_init(&options);

  if (value.is_none()) {
    return options;
  }
  if (!py::isinstance<py::dict>(value)) {
    throw py::type_error("orchestrator options must be a dict when provided");
  }

  py::dict dict = value.cast<py::dict>();
  options.enable_memory_state_storage =
      ReadBoolFromDict(dict, {"memory_state_storage", "memoryStateStorage"}, options.enable_memory_state_storage != 0)
          ? 1
          : 0;
  options.enable_memory_result_storage = ReadBoolFromDict(
                                             dict,
                                             {"memory_result_storage", "memoryResultStorage"},
                                             options.enable_memory_result_storage != 0)
                                             ? 1
                                             : 0;
  return options;
}

TF_RetryPolicy ParseRetryPolicy(const py::object& value) {
  if (!py::isinstance<py::dict>(value)) {
    throw py::type_error("retry policy must be a dict");
  }

  TF_RetryPolicy policy{};
  tf_retry_policy_init(&policy);
  py::dict dict = value.cast<py::dict>();

  py::object max_attempts = GetDictValueAny(dict, {"max_attempts", "maxAttempts"});
  if (!max_attempts.is_none()) {
    policy.max_attempts = py::cast<int32_t>(max_attempts);
  }

  py::object initial_delay = GetDictValueAny(dict, {"initial_delay_ms", "initialDelayMs"});
  if (!initial_delay.is_none()) {
    policy.initial_delay_ms = ReadUint64(initial_delay, "initial_delay_ms");
  }

  py::object backoff = GetDictValueAny(dict, {"backoff_multiplier", "backoffMultiplier"});
  if (!backoff.is_none()) {
    policy.backoff_multiplier = py::cast<double>(backoff);
  }

  py::object max_delay = GetDictValueAny(dict, {"max_delay_ms", "maxDelayMs"});
  if (!max_delay.is_none()) {
    policy.max_delay_ms = ReadUint64(max_delay, "max_delay_ms");
  }

  py::object jitter = GetDictValue(dict, "jitter");
  if (!jitter.is_none()) {
    policy.jitter = py::cast<bool>(jitter) ? 1 : 0;
  }

  py::object jitter_range = GetDictValueAny(dict, {"jitter_range_ms", "jitterRangeMs"});
  if (!jitter_range.is_none()) {
    policy.jitter_range_ms = ReadUint64(jitter_range, "jitter_range_ms");
  }

  return policy;
}

py::object JsonLoads(const std::string& json_text) {
  py::object json = py::module_::import("json").attr("loads");
  return json(py::str(json_text));
}

py::object BuildConstantsObject() {
  py::object namespace_type = py::module_::import("types").attr("SimpleNamespace");
  return namespace_type();
}

py::object ReadGenericValue(const GenericValueReader& reader) {
  if (reader.get_string) {
    char* buffer = nullptr;
    size_t size = 0;
    const int rc = reader.get_string(&buffer, &size);
    if (rc == TF_OK) {
      return py::str(TakeOwnedString(buffer, size));
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      ThrowTaskflowError(rc, "read string value");
    }
  }

  if (reader.get_bool) {
    int value = 0;
    const int rc = reader.get_bool(&value);
    if (rc == TF_OK) {
      return py::bool_(value != 0);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      ThrowTaskflowError(rc, "read bool value");
    }
  }

  if (reader.get_uint64) {
    uint64_t value = 0;
    const int rc = reader.get_uint64(&value);
    if (rc == TF_OK) {
      return py::int_(value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      ThrowTaskflowError(rc, "read uint64 value");
    }
  }

  if (reader.get_int64) {
    int64_t value = 0;
    const int rc = reader.get_int64(&value);
    if (rc == TF_OK) {
      return py::int_(value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      ThrowTaskflowError(rc, "read int64 value");
    }
  }

  if (reader.get_double) {
    double value = 0.0;
    const int rc = reader.get_double(&value);
    if (rc == TF_OK) {
      return py::float_(value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      ThrowTaskflowError(rc, "read double value");
    }
  }

  return py::none();
}

void WriteGenericValue(const py::handle& value, const GenericValueWriter& writer, const char* name) {
  int rc = TF_ERROR_INVALID_ARGUMENT;

  if (py::isinstance<py::bool_>(value)) {
    rc = writer.set_bool ? writer.set_bool(py::cast<bool>(value) ? 1 : 0) : TF_ERROR_NOT_SUPPORTED;
  } else if (py::isinstance<py::int_>(value)) {
    try {
      int64_t signed_value = py::cast<int64_t>(value);
      rc = writer.set_int64 ? writer.set_int64(signed_value) : TF_ERROR_NOT_SUPPORTED;
    } catch (const py::cast_error&) {
      try {
        uint64_t unsigned_value = py::cast<uint64_t>(value);
        rc = writer.set_uint64 ? writer.set_uint64(unsigned_value) : TF_ERROR_NOT_SUPPORTED;
      } catch (const py::cast_error&) {
        throw py::type_error(std::string(name) + " integer is out of range for the native API");
      }
    }
  } else if (py::isinstance<py::float_>(value)) {
    double number = py::cast<double>(value);
    if (!std::isfinite(number)) {
      throw py::type_error(std::string(name) + " must be finite");
    }
    rc = writer.set_double ? writer.set_double(number) : TF_ERROR_NOT_SUPPORTED;
  } else if (py::isinstance<py::str>(value)) {
    std::string text = py::cast<std::string>(value);
    rc = writer.set_string ? writer.set_string(text.c_str()) : TF_ERROR_NOT_SUPPORTED;
  } else {
    throw py::type_error(std::string(name) + " must be a bool, int, float, or string");
  }

  EnsureTaskflowOk(rc, "write native value");
}

class TaskContextProxy {
 public:
  TaskContextProxy(tf_task_context_t ctx, std::shared_ptr<std::atomic<bool>> alive)
      : ctx_(ctx), alive_(std::move(alive)) {}

  uint64_t exec_id() const {
    EnsureAlive();
    uint64_t exec_id = 0;
    EnsureTaskflowOk(tf_task_context_get_exec_id(ctx_, &exec_id), "get task execution id");
    return exec_id;
  }

  uint64_t node_id() const {
    EnsureAlive();
    uint64_t node_id = 0;
    EnsureTaskflowOk(tf_task_context_get_node_id(ctx_, &node_id), "get task node id");
    return node_id;
  }

  bool contains(const std::string& key) const {
    EnsureAlive();
    int contains_value = 0;
    EnsureTaskflowOk(tf_task_context_contains(ctx_, key.c_str(), &contains_value), "check task context key");
    return contains_value != 0;
  }

  py::object get(const std::string& key) const {
    EnsureAlive();
    return ReadGenericValue(
        GenericValueReader{
            [&](char** buffer, size_t* size) { return tf_task_context_get_string_copy(ctx_, key.c_str(), buffer, size); },
            [&](int* value) { return tf_task_context_get_bool(ctx_, key.c_str(), value); },
            [&](uint64_t* value) { return tf_task_context_get_uint64(ctx_, key.c_str(), value); },
            [&](int64_t* value) { return tf_task_context_get_int64(ctx_, key.c_str(), value); },
            [&](double* value) { return tf_task_context_get_double(ctx_, key.c_str(), value); },
        });
  }

  void set(const std::string& key, const py::handle& value) {
    EnsureAlive();
    WriteGenericValue(
        value,
        GenericValueWriter{
            [&](int native_value) { return tf_task_context_set_bool(ctx_, key.c_str(), native_value); },
            [&](int64_t native_value) { return tf_task_context_set_int64(ctx_, key.c_str(), native_value); },
            [&](uint64_t native_value) { return tf_task_context_set_uint64(ctx_, key.c_str(), native_value); },
            [&](double native_value) { return tf_task_context_set_double(ctx_, key.c_str(), native_value); },
            [&](const char* native_value) { return tf_task_context_set_string(ctx_, key.c_str(), native_value); },
        },
        "task context value");
  }

  py::object get_result(uint64_t from_node_id, const std::string& key) const {
    EnsureAlive();
    return ReadGenericValue(
        GenericValueReader{
            [&](char** buffer, size_t* size) {
              return tf_task_context_get_result_string_copy(ctx_, from_node_id, key.c_str(), buffer, size);
            },
            [&](int* value) { return tf_task_context_get_result_bool(ctx_, from_node_id, key.c_str(), value); },
            [&](uint64_t* value) {
              return tf_task_context_get_result_uint64(ctx_, from_node_id, key.c_str(), value);
            },
            [&](int64_t* value) { return tf_task_context_get_result_int64(ctx_, from_node_id, key.c_str(), value); },
            [&](double* value) { return tf_task_context_get_result_double(ctx_, from_node_id, key.c_str(), value); },
        });
  }

  void set_result(const std::string& key, const py::handle& value) {
    EnsureAlive();
    WriteGenericValue(
        value,
        GenericValueWriter{
            [&](int native_value) { return tf_task_context_set_result_bool(ctx_, key.c_str(), native_value); },
            [&](int64_t native_value) { return tf_task_context_set_result_int64(ctx_, key.c_str(), native_value); },
            [&](uint64_t native_value) { return tf_task_context_set_result_uint64(ctx_, key.c_str(), native_value); },
            [&](double native_value) { return tf_task_context_set_result_double(ctx_, key.c_str(), native_value); },
            [&](const char* native_value) { return tf_task_context_set_result_string(ctx_, key.c_str(), native_value); },
        },
        "task result value");
  }

  double progress() const {
    EnsureAlive();
    double progress_value = 0.0;
    EnsureTaskflowOk(tf_task_context_get_progress(ctx_, &progress_value), "get task progress");
    return progress_value;
  }

  void report_progress(double progress_value) {
    EnsureAlive();
    EnsureTaskflowOk(tf_task_context_report_progress(ctx_, progress_value), "report task progress");
  }

  bool is_cancelled() const {
    EnsureAlive();
    int cancelled = 0;
    EnsureTaskflowOk(tf_task_context_is_cancelled(ctx_, &cancelled), "check task cancellation");
    return cancelled != 0;
  }

  void cancel() {
    EnsureAlive();
    EnsureTaskflowOk(tf_task_context_request_cancel(ctx_), "cancel task");
  }

  void set_error(const std::string& message) {
    EnsureAlive();
    EnsureTaskflowOk(tf_task_context_set_error(ctx_, message.c_str()), "set task error");
  }

 private:
  void EnsureAlive() const {
    if (!ctx_ || !alive_ || !alive_->load()) {
      throw std::runtime_error("TaskContext is no longer valid outside the callback that received it");
    }
  }

  tf_task_context_t ctx_ = nullptr;
  std::shared_ptr<std::atomic<bool>> alive_;
};

int NormalizeTaskReturn(const py::handle& result) {
  if (result.is_none()) {
    return TF_TASK_STATE_SUCCESS;
  }
  if (py::isinstance<py::bool_>(result)) {
    return py::cast<bool>(result) ? TF_TASK_STATE_SUCCESS : TF_TASK_STATE_FAILED;
  }
  if (py::isinstance<py::str>(result)) {
    const std::string state = py::cast<std::string>(result);
    if (state == "success") {
      return TF_TASK_STATE_SUCCESS;
    }
    if (state == "failed") {
      return TF_TASK_STATE_FAILED;
    }
    if (state == "cancelled") {
      return TF_TASK_STATE_CANCELLED;
    }
    if (state == "skipped") {
      return TF_TASK_STATE_SKIPPED;
    }
    throw py::type_error("task callback string result must be success, failed, cancelled, or skipped");
  }
  if (py::isinstance<py::int_>(result)) {
    return py::cast<int>(result);
  }

  throw py::type_error("task callback must return an int, bool, string, or None");
}

int InvokePythonTaskCallback(PyTaskRegistration& registration, tf_task_context_t ctx) {
  py::gil_scoped_acquire gil;

  auto alive = std::make_shared<std::atomic<bool>>(true);
  auto context = std::make_shared<TaskContextProxy>(ctx, alive);

  try {
    py::object result = registration.function(context);
    alive->store(false);
    return NormalizeTaskReturn(result);
  } catch (const py::error_already_set& error) {
    alive->store(false);
    (void)tf_task_context_set_error(ctx, error.what());
    return TF_TASK_STATE_FAILED;
  } catch (const std::exception& error) {
    alive->store(false);
    (void)tf_task_context_set_error(ctx, error.what());
    return TF_TASK_STATE_FAILED;
  }
}

bool InvokePythonConditionCallback(PyConditionRegistration& registration, tf_task_context_t ctx) {
  py::gil_scoped_acquire gil;

  auto alive = std::make_shared<std::atomic<bool>>(true);
  auto context = std::make_shared<TaskContextProxy>(ctx, alive);

  try {
    py::object result = registration.function(context);
    alive->store(false);
    return py::cast<bool>(result);
  } catch (const py::error_already_set&) {
    alive->store(false);
    return false;
  } catch (const std::exception&) {
    alive->store(false);
    return false;
  }
}

int PythonTaskCallbackBridge(tf_task_context_t ctx, void* userdata) {
  auto* registration = static_cast<PyTaskRegistration*>(userdata);
  return registration ? InvokePythonTaskCallback(*registration, ctx) : TF_TASK_STATE_FAILED;
}

int PythonConditionCallbackBridge(tf_task_context_t ctx, void* userdata) {
  auto* registration = static_cast<PyConditionRegistration*>(userdata);
  return registration && InvokePythonConditionCallback(*registration, ctx) ? 1 : 0;
}

class PyBlueprint {
 public:
  explicit PyBlueprint(py::object definition = py::none()) { Create(); if (!definition.is_none()) load(definition); }

  ~PyBlueprint() {
    try {
      destroy_no_throw();
    } catch (...) {
    }
  }

  uint64_t handle() const { return handle_; }
  bool closed() const { return closed_; }

  PyBlueprint& load(py::object definition) {
    ensure_open();

    if (py::isinstance<py::str>(definition)) {
      definition = JsonLoads(py::cast<std::string>(definition));
    }
    if (!py::isinstance<py::dict>(definition)) {
      throw py::type_error("blueprint definition must be a dict or JSON string");
    }

    py::dict dict = definition.cast<py::dict>();
    py::object nodes = GetDictValue(dict, "nodes");
    if (!nodes.is_none()) {
      for (py::handle node : nodes) {
        add_node(py::reinterpret_borrow<py::object>(node), py::none(), py::none());
      }
    }

    py::object edges = GetDictValue(dict, "edges");
    if (!edges.is_none()) {
      for (py::handle edge : edges) {
        add_edge(py::reinterpret_borrow<py::object>(edge), py::none(), py::none());
      }
    }
    return *this;
  }

  PyBlueprint& add_node(py::object node_or_id, py::object task_type = py::none(), py::object options = py::none()) {
    ensure_open();

    uint64_t node_id = 0;
    std::string task_type_text;
    py::object label = py::none();
    py::object retry = py::none();
    py::object compensate_task_type = py::none();
    py::object compensate_retry = py::none();
    py::object tags = py::none();

    if (py::isinstance<py::dict>(node_or_id)) {
      py::dict node = node_or_id.cast<py::dict>();
      py::object id_value = GetDictValue(node, "id");
      if (id_value.is_none()) {
        throw py::type_error("blueprint node requires id");
      }
      node_id = ReadUint64(id_value, "id");

      py::object task_value = GetDictValueAny(node, {"task_type", "taskType"});
      if (task_value.is_none()) {
        throw py::type_error("blueprint node requires task_type or taskType");
      }
      task_type_text = ReadString(task_value, "task_type");

      label = GetDictValue(node, "label");
      retry = GetDictValue(node, "retry");
      compensate_task_type = GetDictValueAny(node, {"compensate_task_type", "compensateTaskType"});
      compensate_retry = GetDictValueAny(node, {"compensate_retry", "compensateRetry"});
      tags = GetDictValue(node, "tags");
    } else {
      node_id = ReadUint64(node_or_id, "node_id");
      if (task_type.is_none()) {
        throw py::type_error("task_type must be provided when add_node is called with a node id");
      }
      task_type_text = ReadString(task_type, "task_type");

      if (!options.is_none()) {
        if (!py::isinstance<py::dict>(options)) {
          throw py::type_error("node options must be a dict");
        }
        py::dict opts = options.cast<py::dict>();
        label = GetDictValue(opts, "label");
        retry = GetDictValue(opts, "retry");
        compensate_task_type = GetDictValueAny(opts, {"compensate_task_type", "compensateTaskType"});
        compensate_retry = GetDictValueAny(opts, {"compensate_retry", "compensateRetry"});
        tags = GetDictValue(opts, "tags");
      }
    }

    const char* label_ptr = nullptr;
    std::string label_storage;
    if (!label.is_none()) {
      label_storage = ReadString(label, "label");
      label_ptr = label_storage.c_str();
    }

    EnsureTaskflowOk(tf_blueprint_add_node(handle_, node_id, task_type_text.c_str(), label_ptr), "add blueprint node");
    add_task_type(task_type_text);

    if (!retry.is_none()) {
      TF_RetryPolicy retry_policy = ParseRetryPolicy(retry);
      EnsureTaskflowOk(tf_blueprint_set_node_retry(handle_, node_id, &retry_policy), "set node retry");
    }

    if (!compensate_task_type.is_none() || !compensate_retry.is_none()) {
      const char* compensate_type_ptr = nullptr;
      std::string compensate_type_storage;
      if (!compensate_task_type.is_none()) {
        compensate_type_storage = ReadString(compensate_task_type, "compensate_task_type");
        compensate_type_ptr = compensate_type_storage.c_str();
      }

      TF_RetryPolicy compensation_policy{};
      TF_RetryPolicy* compensation_policy_ptr = nullptr;
      if (!compensate_retry.is_none()) {
        compensation_policy = ParseRetryPolicy(compensate_retry);
        compensation_policy_ptr = &compensation_policy;
      }

      EnsureTaskflowOk(
          tf_blueprint_set_node_compensation(handle_, node_id, compensate_type_ptr, compensation_policy_ptr),
          "set node compensation");
    }

    if (!tags.is_none()) {
      for (py::handle tag : tags) {
        std::string tag_text = ReadString(tag, "tag");
        EnsureTaskflowOk(tf_blueprint_add_node_tag(handle_, node_id, tag_text.c_str()), "add node tag");
      }
    }

    return *this;
  }

  PyBlueprint& add_edge(py::object edge_or_from, py::object to_node_id = py::none(), py::object maybe_condition = py::none()) {
    ensure_open();

    uint64_t from_node_id = 0;
    uint64_t to_node_id_value = 0;
    py::object condition = py::none();

    if (py::isinstance<py::dict>(edge_or_from)) {
      py::dict edge = edge_or_from.cast<py::dict>();
      py::object from_value = GetDictValue(edge, "from");
      py::object to_value = GetDictValue(edge, "to");
      if (from_value.is_none() || to_value.is_none()) {
        throw py::type_error("blueprint edge requires from and to");
      }

      from_node_id = ReadUint64(from_value, "from");
      to_node_id_value = ReadUint64(to_value, "to");
      condition = GetDictValueAny(edge, {"condition", "when"});
    } else {
      from_node_id = ReadUint64(edge_or_from, "from_node_id");
      if (to_node_id.is_none()) {
        throw py::type_error("to_node_id must be provided when add_edge is called with node ids");
      }
      to_node_id_value = ReadUint64(to_node_id, "to_node_id");
      condition = maybe_condition;
    }

    if (!condition.is_none()) {
      if (!PyCallable_Check(condition.ptr())) {
        throw py::type_error("edge condition must be callable");
      }

      auto registration = std::make_shared<PyConditionRegistration>(py::reinterpret_borrow<py::function>(condition));
      EnsureTaskflowOk(
          tf_blueprint_add_conditional_edge(
              handle_,
              from_node_id,
              to_node_id_value,
              PythonConditionCallbackBridge,
              registration.get()),
          "add conditional blueprint edge");
      condition_registrations_.push_back(registration);
      requires_async_execution_ = true;
      return *this;
    }

    EnsureTaskflowOk(tf_blueprint_add_edge(handle_, from_node_id, to_node_id_value), "add blueprint edge");
    return *this;
  }

  std::string validate() const {
    ensure_open();
    char* message = nullptr;
    size_t size = 0;
    EnsureTaskflowOk(tf_blueprint_validate_copy(handle_, &message, &size), "validate blueprint");
    return TakeOwnedString(message, size);
  }

  bool is_valid() const {
    ensure_open();
    int valid = 0;
    EnsureTaskflowOk(tf_blueprint_is_valid(handle_, &valid), "check blueprint validity");
    return valid != 0;
  }

  std::string to_json() const {
    ensure_open();
    char* json = nullptr;
    size_t size = 0;
    EnsureTaskflowOk(tf_blueprint_to_json_copy(handle_, &json, &size), "serialize blueprint");
    return TakeOwnedString(json, size);
  }

  py::object to_object() const { return JsonLoads(to_json()); }

  py::dict metadata() const {
    py::dict result;
    py::list task_types;
    for (const auto& task_type : task_types_) {
      task_types.append(py::str(task_type));
    }
    result["taskTypes"] = task_types;
    result["requiresAsyncExecution"] = py::bool_(requires_async_execution_);
    return result;
  }

  void destroy() {
    ensure_open();
    EnsureTaskflowOk(tf_blueprint_destroy(handle_), "destroy blueprint");
    closed_ = true;
    condition_registrations_.clear();
  }

  const std::vector<std::shared_ptr<PyConditionRegistration>>& condition_registrations() const {
    return condition_registrations_;
  }

 private:
  void Create() {
    EnsureTaskflowOk(tf_blueprint_create(&handle_), "create blueprint");
  }

  void ensure_open() const {
    if (closed_) {
      throw std::runtime_error("Blueprint is already closed");
    }
  }

  void destroy_no_throw() {
    if (!closed_ && handle_ != 0) {
      (void)tf_blueprint_destroy(handle_);
      closed_ = true;
    }
    condition_registrations_.clear();
  }

  void add_task_type(const std::string& task_type) {
    for (const auto& existing : task_types_) {
      if (existing == task_type) {
        return;
      }
    }
    task_types_.push_back(task_type);
  }

  tf_blueprint_t handle_ = 0;
  bool closed_ = false;
  bool requires_async_execution_ = false;
  std::vector<std::string> task_types_;
  std::vector<std::shared_ptr<PyConditionRegistration>> condition_registrations_;
};

class PyExecution {
 public:
  explicit PyExecution(tf_execution_t handle) : handle_(handle) {}

  ~PyExecution() {
    try {
      destroy_no_throw();
    } catch (...) {
    }
  }

  uint64_t handle() const { return handle_; }
  bool closed() const { return closed_; }

  uint64_t id() const {
    ensure_open();
    uint64_t execution_id = 0;
    EnsureTaskflowOk(tf_execution_get_id(handle_, &execution_id), "get execution id");
    return execution_id;
  }

  std::string snapshot_json() const {
    ensure_open();
    char* json = nullptr;
    size_t size = 0;
    EnsureTaskflowOk(tf_execution_snapshot_json_copy(handle_, &json, &size), "get execution snapshot");
    return TakeOwnedString(json, size);
  }

  py::object snapshot() const { return JsonLoads(snapshot_json()); }

  int get_node_state(uint64_t node_id) const {
    ensure_open();
    int state = TF_TASK_STATE_PENDING;
    EnsureTaskflowOk(tf_execution_get_node_state(handle_, node_id, &state), "get node state");
    return state;
  }

  std::string get_node_state_name(uint64_t node_id) const { return std::string(tf_task_state_name(get_node_state(node_id))); }

  int get_overall_state() const {
    ensure_open();
    int state = TF_TASK_STATE_PENDING;
    EnsureTaskflowOk(tf_execution_get_overall_state(handle_, &state), "get overall execution state");
    return state;
  }

  std::string get_overall_state_name() const { return std::string(tf_task_state_name(get_overall_state())); }

  PyExecution& set(const std::string& key, const py::handle& value) {
    ensure_open();
    WriteGenericValue(
        value,
        GenericValueWriter{
            [&](int native_value) { return tf_execution_context_set_bool(handle_, key.c_str(), native_value); },
            [&](int64_t native_value) { return tf_execution_context_set_int64(handle_, key.c_str(), native_value); },
            [&](uint64_t native_value) { return tf_execution_context_set_uint64(handle_, key.c_str(), native_value); },
            [&](double native_value) { return tf_execution_context_set_double(handle_, key.c_str(), native_value); },
            [&](const char* native_value) { return tf_execution_context_set_string(handle_, key.c_str(), native_value); },
        },
        "execution context value");
    return *this;
  }

  py::object get(const std::string& key) const {
    ensure_open();
    return ReadGenericValue(
        GenericValueReader{
            [&](char** buffer, size_t* size) {
              return tf_execution_context_get_string_copy(handle_, key.c_str(), buffer, size);
            },
            [&](int* value) { return tf_execution_context_get_bool(handle_, key.c_str(), value); },
            [&](uint64_t* value) { return tf_execution_context_get_uint64(handle_, key.c_str(), value); },
            [&](int64_t* value) { return tf_execution_context_get_int64(handle_, key.c_str(), value); },
            [&](double* value) { return tf_execution_context_get_double(handle_, key.c_str(), value); },
        });
  }

  py::object get_result(uint64_t from_node_id, const std::string& key) const {
    ensure_open();
    return ReadGenericValue(
        GenericValueReader{
            [&](char** buffer, size_t* size) {
              return tf_execution_result_get_string_copy(handle_, from_node_id, key.c_str(), buffer, size);
            },
            [&](int* value) { return tf_execution_result_get_bool(handle_, from_node_id, key.c_str(), value); },
            [&](uint64_t* value) { return tf_execution_result_get_uint64(handle_, from_node_id, key.c_str(), value); },
            [&](int64_t* value) { return tf_execution_result_get_int64(handle_, from_node_id, key.c_str(), value); },
            [&](double* value) { return tf_execution_result_get_double(handle_, from_node_id, key.c_str(), value); },
        });
  }

  int run_sync(py::object options = py::none()) {
    ensure_open();
    TF_RunOptions native_options = ParseRunOptions(options);
    int state = TF_TASK_STATE_PENDING;
    {
      py::gil_scoped_release release;
      EnsureTaskflowOk(tf_execution_run_sync(handle_, &native_options, &state), "run execution synchronously");
    }
    return state;
  }

  py::dict poll() const {
    ensure_open();
    int ready = 0;
    int state = TF_TASK_STATE_PENDING;
    EnsureTaskflowOk(tf_execution_poll(handle_, &ready, &state), "poll execution");

    py::dict result;
    result["ready"] = py::bool_(ready != 0);
    result["state"] = py::int_(state);
    result["state_name"] = py::str(tf_task_state_name(state));
    return result;
  }

  int wait() const {
    ensure_open();
    int state = TF_TASK_STATE_PENDING;
    {
      py::gil_scoped_release release;
      EnsureTaskflowOk(tf_execution_wait(handle_, &state), "wait for execution");
    }
    return state;
  }

  void cancel() {
    ensure_open();
    EnsureTaskflowOk(tf_execution_cancel(handle_), "cancel execution");
  }

  bool is_complete() const {
    ensure_open();
    int complete = 0;
    EnsureTaskflowOk(tf_execution_is_complete(handle_, &complete), "check execution completion");
    return complete != 0;
  }

  bool is_cancelled() const {
    ensure_open();
    int cancelled = 0;
    EnsureTaskflowOk(tf_execution_is_cancelled(handle_, &cancelled), "check execution cancellation");
    return cancelled != 0;
  }

  void destroy() {
    ensure_open();
    EnsureTaskflowOk(tf_execution_destroy(handle_), "destroy execution");
    closed_ = true;
  }

 private:
  void ensure_open() const {
    if (closed_) {
      throw std::runtime_error("Execution is already closed");
    }
  }

  void destroy_no_throw() {
    if (!closed_ && handle_ != 0) {
      (void)tf_execution_destroy(handle_);
      closed_ = true;
    }
  }

  tf_execution_t handle_ = 0;
  bool closed_ = false;
};

class PyOrchestrator {
 public:
  explicit PyOrchestrator(py::object options = py::none()) {
    TF_OrchestratorOptions native_options = ParseOrchestratorOptions(options);
    EnsureTaskflowOk(tf_orchestrator_create_ex(&native_options, &handle_), "create orchestrator");
  }

  ~PyOrchestrator() {
    try {
      destroy_no_throw();
    } catch (...) {
    }
  }

  uint64_t handle() const { return handle_; }
  bool closed() const { return closed_; }

  PyOrchestrator& register_task(const std::string& task_type, py::function callback) {
    ensure_open();
    if (task_type.empty()) {
      throw py::type_error("task_type must be a non-empty string");
    }

    auto registration = std::make_shared<PyTaskRegistration>(std::move(callback));
    EnsureTaskflowOk(
        tf_orchestrator_register_task_callback(handle_, task_type.c_str(), PythonTaskCallbackBridge, registration.get()),
        "register task callback");
    task_registrations_.push_back(registration);
    return *this;
  }

  bool has_task(const std::string& task_type) const {
    ensure_open();
    int has_task = 0;
    EnsureTaskflowOk(tf_orchestrator_has_task(handle_, task_type.c_str(), &has_task), "query task registration");
    return has_task != 0;
  }

  PyOrchestrator& register_blueprint(py::object id_or_name, py::object blueprint_definition) {
    ensure_open();

    if (py::isinstance<PyBlueprint>(blueprint_definition)) {
      auto& blueprint = blueprint_definition.cast<PyBlueprint&>();
      register_blueprint_handle(id_or_name, blueprint.handle());
      extend_condition_registrations(blueprint.condition_registrations());
      return *this;
    }

    PyBlueprint temporary(blueprint_definition);
    register_blueprint_handle(id_or_name, temporary.handle());
    extend_condition_registrations(temporary.condition_registrations());
    return *this;
  }

  bool has_blueprint(py::object id_or_name) const {
    ensure_open();

    int has_blueprint = 0;
    if (py::isinstance<py::str>(id_or_name)) {
      std::string blueprint_name = py::cast<std::string>(id_or_name);
      EnsureTaskflowOk(
          tf_orchestrator_has_blueprint_name(handle_, blueprint_name.c_str(), &has_blueprint),
          "query named blueprint");
    } else {
      uint64_t blueprint_id = ReadUint64(id_or_name, "blueprint_id");
      EnsureTaskflowOk(tf_orchestrator_has_blueprint(handle_, blueprint_id, &has_blueprint), "query blueprint");
    }

    return has_blueprint != 0;
  }

  std::string get_blueprint_json(py::object id_or_name) const {
    ensure_open();

    char* json = nullptr;
    size_t size = 0;
    if (py::isinstance<py::str>(id_or_name)) {
      std::string blueprint_name = py::cast<std::string>(id_or_name);
      EnsureTaskflowOk(
          tf_orchestrator_get_blueprint_name_json_copy(handle_, blueprint_name.c_str(), &json, &size),
          "get named blueprint json");
    } else {
      uint64_t blueprint_id = ReadUint64(id_or_name, "blueprint_id");
      EnsureTaskflowOk(
          tf_orchestrator_get_blueprint_json_copy(handle_, blueprint_id, &json, &size),
          "get blueprint json");
    }

    return TakeOwnedString(json, size);
  }

  py::object get_blueprint(py::object id_or_name) const { return JsonLoads(get_blueprint_json(id_or_name)); }

  std::shared_ptr<PyExecution> create_execution(py::object id_or_name) const {
    ensure_open();

    tf_execution_t execution = 0;
    if (py::isinstance<py::str>(id_or_name)) {
      std::string blueprint_name = py::cast<std::string>(id_or_name);
      EnsureTaskflowOk(
          tf_orchestrator_create_execution_name(handle_, blueprint_name.c_str(), &execution),
          "create named execution");
    } else {
      uint64_t blueprint_id = ReadUint64(id_or_name, "blueprint_id");
      EnsureTaskflowOk(tf_orchestrator_create_execution(handle_, blueprint_id, &execution), "create execution");
    }

    return std::make_shared<PyExecution>(execution);
  }

  std::shared_ptr<PyExecution> run_sync(py::object id_or_name, py::object options = py::none()) const {
    ensure_open();

    TF_RunOptions native_options = ParseRunOptions(options);
    tf_execution_t execution = 0;
    const bool use_name = py::isinstance<py::str>(id_or_name);
    std::string blueprint_name;
    uint64_t blueprint_id = 0;
    if (use_name) {
      blueprint_name = py::cast<std::string>(id_or_name);
    } else {
      blueprint_id = ReadUint64(id_or_name, "blueprint_id");
    }

    {
      py::gil_scoped_release release;
      if (use_name) {
        EnsureTaskflowOk(
            tf_orchestrator_run_sync_name(handle_, blueprint_name.c_str(), &native_options, &execution),
            "run named workflow synchronously");
      } else {
        EnsureTaskflowOk(
            tf_orchestrator_run_sync(handle_, blueprint_id, &native_options, &execution),
            "run workflow synchronously");
      }
    }

    return std::make_shared<PyExecution>(execution);
  }

  std::shared_ptr<PyExecution> run_async(py::object id_or_name, py::object options = py::none()) const {
    ensure_open();

    TF_RunOptions native_options = ParseRunOptions(options);
    tf_execution_t execution = 0;
    if (py::isinstance<py::str>(id_or_name)) {
      std::string blueprint_name = py::cast<std::string>(id_or_name);
      EnsureTaskflowOk(
          tf_orchestrator_run_async_name(handle_, blueprint_name.c_str(), &native_options, &execution),
          "run named workflow asynchronously");
    } else {
      uint64_t blueprint_id = ReadUint64(id_or_name, "blueprint_id");
      EnsureTaskflowOk(
          tf_orchestrator_run_async(handle_, blueprint_id, &native_options, &execution),
          "run workflow asynchronously");
    }

    return std::make_shared<PyExecution>(execution);
  }

  size_t cleanup_completed_executions() const {
    ensure_open();
    size_t count = 0;
    EnsureTaskflowOk(
        tf_orchestrator_cleanup_completed_executions(handle_, &count),
        "cleanup completed executions");
    return count;
  }

  void destroy() {
    ensure_open();
    EnsureTaskflowOk(tf_orchestrator_destroy(handle_), "destroy orchestrator");
    closed_ = true;
    task_registrations_.clear();
    condition_registrations_.clear();
  }

 private:
  void ensure_open() const {
    if (closed_) {
      throw std::runtime_error("Orchestrator is already closed");
    }
  }

  void destroy_no_throw() {
    if (!closed_ && handle_ != 0) {
      (void)tf_orchestrator_destroy(handle_);
      closed_ = true;
    }
    task_registrations_.clear();
    condition_registrations_.clear();
  }

  void extend_condition_registrations(const std::vector<std::shared_ptr<PyConditionRegistration>>& registrations) {
    condition_registrations_.insert(condition_registrations_.end(), registrations.begin(), registrations.end());
  }

  void register_blueprint_handle(py::object id_or_name, tf_blueprint_t blueprint_handle) const {
    if (py::isinstance<py::str>(id_or_name)) {
      std::string blueprint_name = py::cast<std::string>(id_or_name);
      EnsureTaskflowOk(
          tf_orchestrator_register_blueprint_name(handle_, blueprint_name.c_str(), blueprint_handle),
          "register named blueprint");
      return;
    }

    uint64_t blueprint_id = ReadUint64(id_or_name, "blueprint_id");
    EnsureTaskflowOk(
        tf_orchestrator_register_blueprint(handle_, blueprint_id, blueprint_handle),
        "register blueprint");
  }

  tf_orchestrator_t handle_ = 0;
  bool closed_ = false;
  std::vector<std::shared_ptr<PyTaskRegistration>> task_registrations_;
  std::vector<std::shared_ptr<PyConditionRegistration>> condition_registrations_;
};

void AddModuleConstant(py::module_& module, py::object& constants, const char* name, int value) {
  module.attr(name) = py::int_(value);
  py::setattr(constants, name, py::int_(value));
}

}  // namespace

PYBIND11_MODULE(taskflow, m) {
  m.doc() = "TaskFlow Python bindings powered by pybind11";

  py::class_<TaskContextProxy, std::shared_ptr<TaskContextProxy>>(m, "TaskContext")
      .def("exec_id", &TaskContextProxy::exec_id)
      .def("execId", &TaskContextProxy::exec_id)
      .def("node_id", &TaskContextProxy::node_id)
      .def("nodeId", &TaskContextProxy::node_id)
      .def("contains", &TaskContextProxy::contains, py::arg("key"))
      .def("get", &TaskContextProxy::get, py::arg("key"))
      .def("set", &TaskContextProxy::set, py::arg("key"), py::arg("value"))
      .def("get_result", &TaskContextProxy::get_result, py::arg("from_node_id"), py::arg("key"))
      .def("getResult", &TaskContextProxy::get_result, py::arg("from_node_id"), py::arg("key"))
      .def("set_result", &TaskContextProxy::set_result, py::arg("key"), py::arg("value"))
      .def("setResult", &TaskContextProxy::set_result, py::arg("key"), py::arg("value"))
      .def("progress", &TaskContextProxy::progress)
      .def("report_progress", &TaskContextProxy::report_progress, py::arg("progress"))
      .def("reportProgress", &TaskContextProxy::report_progress, py::arg("progress"))
      .def("is_cancelled", &TaskContextProxy::is_cancelled)
      .def("isCancelled", &TaskContextProxy::is_cancelled)
      .def("cancel", &TaskContextProxy::cancel)
      .def("set_error", &TaskContextProxy::set_error, py::arg("message"))
      .def("setError", &TaskContextProxy::set_error, py::arg("message"));

  py::class_<PyBlueprint>(m, "Blueprint")
      .def(py::init<py::object>(), py::arg("definition") = py::none())
      .def_property_readonly("handle", &PyBlueprint::handle)
      .def_property_readonly("closed", &PyBlueprint::closed)
      .def("load", &PyBlueprint::load, py::arg("definition"), py::return_value_policy::reference_internal)
      .def("add_node", &PyBlueprint::add_node, py::arg("node_or_id"), py::arg("task_type") = py::none(),
           py::arg("options") = py::none(), py::return_value_policy::reference_internal)
      .def("addNode", &PyBlueprint::add_node, py::arg("node_or_id"), py::arg("task_type") = py::none(),
           py::arg("options") = py::none(), py::return_value_policy::reference_internal)
      .def("add_edge", &PyBlueprint::add_edge, py::arg("edge_or_from"), py::arg("to_node_id") = py::none(),
           py::arg("condition") = py::none(), py::return_value_policy::reference_internal)
      .def("addEdge", &PyBlueprint::add_edge, py::arg("edge_or_from"), py::arg("to_node_id") = py::none(),
           py::arg("condition") = py::none(), py::return_value_policy::reference_internal)
      .def("validate", &PyBlueprint::validate)
      .def("is_valid", &PyBlueprint::is_valid)
      .def("isValid", &PyBlueprint::is_valid)
      .def("to_json", &PyBlueprint::to_json)
      .def("toJSON", &PyBlueprint::to_json)
      .def("to_object", &PyBlueprint::to_object)
      .def("toObject", &PyBlueprint::to_object)
      .def("metadata", &PyBlueprint::metadata)
      .def("destroy", &PyBlueprint::destroy);

  py::class_<PyExecution, std::shared_ptr<PyExecution>>(m, "Execution")
      .def_property_readonly("handle", &PyExecution::handle)
      .def_property_readonly("closed", &PyExecution::closed)
      .def("id", &PyExecution::id)
      .def("snapshot_json", &PyExecution::snapshot_json)
      .def("snapshotJSON", &PyExecution::snapshot_json)
      .def("snapshot", &PyExecution::snapshot)
      .def("get_node_state", &PyExecution::get_node_state, py::arg("node_id"))
      .def("getNodeState", &PyExecution::get_node_state, py::arg("node_id"))
      .def("get_node_state_name", &PyExecution::get_node_state_name, py::arg("node_id"))
      .def("getNodeStateName", &PyExecution::get_node_state_name, py::arg("node_id"))
      .def("get_overall_state", &PyExecution::get_overall_state)
      .def("getOverallState", &PyExecution::get_overall_state)
      .def("get_overall_state_name", &PyExecution::get_overall_state_name)
      .def("getOverallStateName", &PyExecution::get_overall_state_name)
      .def("set", &PyExecution::set, py::arg("key"), py::arg("value"), py::return_value_policy::reference_internal)
      .def("get", &PyExecution::get, py::arg("key"))
      .def("get_result", &PyExecution::get_result, py::arg("from_node_id"), py::arg("key"))
      .def("getResult", &PyExecution::get_result, py::arg("from_node_id"), py::arg("key"))
      .def("run_sync", &PyExecution::run_sync, py::arg("options") = py::none())
      .def("runSync", &PyExecution::run_sync, py::arg("options") = py::none())
      .def("poll", &PyExecution::poll)
      .def("wait", &PyExecution::wait)
      .def("cancel", &PyExecution::cancel)
      .def("is_complete", &PyExecution::is_complete)
      .def("isComplete", &PyExecution::is_complete)
      .def("is_cancelled", &PyExecution::is_cancelled)
      .def("isCancelled", &PyExecution::is_cancelled)
      .def("destroy", &PyExecution::destroy);

  py::class_<PyOrchestrator>(m, "Orchestrator")
      .def(py::init<py::object>(), py::arg("options") = py::none())
      .def_property_readonly("handle", &PyOrchestrator::handle)
      .def_property_readonly("closed", &PyOrchestrator::closed)
      .def("register_task", &PyOrchestrator::register_task, py::arg("task_type"), py::arg("callback"),
           py::return_value_policy::reference_internal)
      .def("registerTask", &PyOrchestrator::register_task, py::arg("task_type"), py::arg("callback"),
           py::return_value_policy::reference_internal)
      .def("has_task", &PyOrchestrator::has_task, py::arg("task_type"))
      .def("hasTask", &PyOrchestrator::has_task, py::arg("task_type"))
      .def("register_blueprint", &PyOrchestrator::register_blueprint, py::arg("id_or_name"), py::arg("blueprint"),
           py::return_value_policy::reference_internal)
      .def("registerBlueprint", &PyOrchestrator::register_blueprint, py::arg("id_or_name"), py::arg("blueprint"),
           py::return_value_policy::reference_internal)
      .def("has_blueprint", &PyOrchestrator::has_blueprint, py::arg("id_or_name"))
      .def("hasBlueprint", &PyOrchestrator::has_blueprint, py::arg("id_or_name"))
      .def("get_blueprint_json", &PyOrchestrator::get_blueprint_json, py::arg("id_or_name"))
      .def("getBlueprintJSON", &PyOrchestrator::get_blueprint_json, py::arg("id_or_name"))
      .def("get_blueprint", &PyOrchestrator::get_blueprint, py::arg("id_or_name"))
      .def("getBlueprint", &PyOrchestrator::get_blueprint, py::arg("id_or_name"))
      .def("create_execution", &PyOrchestrator::create_execution, py::arg("id_or_name"))
      .def("createExecution", &PyOrchestrator::create_execution, py::arg("id_or_name"))
      .def("run_sync", &PyOrchestrator::run_sync, py::arg("id_or_name"), py::arg("options") = py::none())
      .def("runSync", &PyOrchestrator::run_sync, py::arg("id_or_name"), py::arg("options") = py::none())
      .def("run_async", &PyOrchestrator::run_async, py::arg("id_or_name"), py::arg("options") = py::none())
      .def("runAsync", &PyOrchestrator::run_async, py::arg("id_or_name"), py::arg("options") = py::none())
      .def("cleanup_completed_executions", &PyOrchestrator::cleanup_completed_executions)
      .def("cleanupCompletedExecutions", &PyOrchestrator::cleanup_completed_executions)
      .def("destroy", &PyOrchestrator::destroy);

  m.def("error_message", [](int code) { return std::string(tf_error_message(code)); }, py::arg("code"));
  m.def("task_state_name", [](int state) { return std::string(tf_task_state_name(state)); }, py::arg("state"));

  py::object constants = BuildConstantsObject();

  AddModuleConstant(m, constants, "TF_OK", TF_OK);
  AddModuleConstant(m, constants, "TF_ERROR_UNKNOWN", TF_ERROR_UNKNOWN);
  AddModuleConstant(m, constants, "TF_ERROR_INVALID_HANDLE", TF_ERROR_INVALID_HANDLE);
  AddModuleConstant(m, constants, "TF_ERROR_BLUEPRINT_NOT_FOUND", TF_ERROR_BLUEPRINT_NOT_FOUND);
  AddModuleConstant(m, constants, "TF_ERROR_EXECUTION_NOT_FOUND", TF_ERROR_EXECUTION_NOT_FOUND);
  AddModuleConstant(m, constants, "TF_ERROR_BUS_SHUTDOWN", TF_ERROR_BUS_SHUTDOWN);
  AddModuleConstant(m, constants, "TF_ERROR_INVALID_STATE", TF_ERROR_INVALID_STATE);
  AddModuleConstant(m, constants, "TF_ERROR_INVALID_ARGUMENT", TF_ERROR_INVALID_ARGUMENT);
  AddModuleConstant(m, constants, "TF_ERROR_TASK_TYPE_NOT_FOUND", TF_ERROR_TASK_TYPE_NOT_FOUND);
  AddModuleConstant(m, constants, "TF_ERROR_OPERATION_NOT_PERMITTED", TF_ERROR_OPERATION_NOT_PERMITTED);
  AddModuleConstant(m, constants, "TF_ERROR_STORAGE", TF_ERROR_STORAGE);
  AddModuleConstant(m, constants, "TF_ERROR_TYPE_MISMATCH", TF_ERROR_TYPE_MISMATCH);
  AddModuleConstant(m, constants, "TF_ERROR_VALUE_NOT_FOUND", TF_ERROR_VALUE_NOT_FOUND);
  AddModuleConstant(m, constants, "TF_ERROR_NOT_SUPPORTED", TF_ERROR_NOT_SUPPORTED);

  AddModuleConstant(m, constants, "TF_TASK_STATE_PENDING", TF_TASK_STATE_PENDING);
  AddModuleConstant(m, constants, "TF_TASK_STATE_RUNNING", TF_TASK_STATE_RUNNING);
  AddModuleConstant(m, constants, "TF_TASK_STATE_SUCCESS", TF_TASK_STATE_SUCCESS);
  AddModuleConstant(m, constants, "TF_TASK_STATE_FAILED", TF_TASK_STATE_FAILED);
  AddModuleConstant(m, constants, "TF_TASK_STATE_RETRY", TF_TASK_STATE_RETRY);
  AddModuleConstant(m, constants, "TF_TASK_STATE_SKIPPED", TF_TASK_STATE_SKIPPED);
  AddModuleConstant(m, constants, "TF_TASK_STATE_CANCELLED", TF_TASK_STATE_CANCELLED);
  AddModuleConstant(m, constants, "TF_TASK_STATE_COMPENSATING", TF_TASK_STATE_COMPENSATING);
  AddModuleConstant(m, constants, "TF_TASK_STATE_COMPENSATED", TF_TASK_STATE_COMPENSATED);
  AddModuleConstant(m, constants, "TF_TASK_STATE_COMPENSATION_FAILED", TF_TASK_STATE_COMPENSATION_FAILED);

  m.attr("constants") = constants;
}
