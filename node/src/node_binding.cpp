#include <napi.h>
#include <taskflow_c.h>

#include <atomic>
#include <cmath>
#include <condition_variable>
#include <cstdint>
#include <functional>
#include <limits>
#include <memory>
#include <mutex>
#include <string>
#include <unordered_map>
#include <utility>
#include <vector>

using namespace Napi;

namespace {

struct TaskContextPayload {
  tf_task_context_t ctx = nullptr;
  std::shared_ptr<std::atomic<bool>> alive = std::make_shared<std::atomic<bool>>(true);
};

class TaskContextProxy : public Napi::ObjectWrap<TaskContextProxy> {
 public:
  static Napi::FunctionReference constructor;

  static void Init(Napi::Env env, Napi::Object exports) {
    Napi::Function ctor = DefineClass(env, "TaskContext",
                                      {
                                          InstanceMethod("execId", &TaskContextProxy::ExecId),
                                          InstanceMethod("nodeId", &TaskContextProxy::NodeId),
                                          InstanceMethod("contains", &TaskContextProxy::Contains),
                                          InstanceMethod("get", &TaskContextProxy::Get),
                                          InstanceMethod("set", &TaskContextProxy::Set),
                                          InstanceMethod("getResult", &TaskContextProxy::GetResult),
                                          InstanceMethod("setResult", &TaskContextProxy::SetResult),
                                          InstanceMethod("progress", &TaskContextProxy::Progress),
                                          InstanceMethod("reportProgress", &TaskContextProxy::ReportProgress),
                                          InstanceMethod("isCancelled", &TaskContextProxy::IsCancelled),
                                          InstanceMethod("cancel", &TaskContextProxy::Cancel),
                                          InstanceMethod("setError", &TaskContextProxy::SetError),
                                      });

    constructor = Napi::Persistent(ctor);
    constructor.SuppressDestruct();
    exports.Set("TaskContext", ctor);
  }

  static Napi::Object NewInstance(Napi::Env env, TaskContextPayload* payload) {
    return constructor.New({Napi::External<TaskContextPayload>::New(env, payload)});
  }

  TaskContextProxy(const Napi::CallbackInfo& info) : Napi::ObjectWrap<TaskContextProxy>(info) {
    if (info.Length() < 1 || !info[0].IsExternal()) {
      Napi::TypeError::New(info.Env(), "TaskContext cannot be constructed directly").ThrowAsJavaScriptException();
      return;
    }

    payload_ = info[0].As<Napi::External<TaskContextPayload>>().Data();
  }

  ~TaskContextProxy() override { delete payload_; }

 private:
  bool EnsureAlive(Napi::Env env) const {
    if (!payload_ || !payload_->ctx || !payload_->alive || !payload_->alive->load()) {
      Napi::Error::New(env, "TaskContext is no longer valid outside the callback that received it")
          .ThrowAsJavaScriptException();
      return false;
    }
    return true;
  }

  Napi::Value ExecId(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!EnsureAlive(env)) {
      return env.Null();
    }

    uint64_t exec_id = 0;
    const int rc = tf_task_context_get_exec_id(payload_->ctx, &exec_id);
    if (rc != TF_OK) {
      Napi::Error::New(env, tf_error_message(rc)).ThrowAsJavaScriptException();
      return env.Null();
    }
    return Napi::BigInt::New(env, exec_id);
  }

  Napi::Value NodeId(const Napi::CallbackInfo& info) {
    Napi::Env env = info.Env();
    if (!EnsureAlive(env)) {
      return env.Null();
    }

    uint64_t node_id = 0;
    const int rc = tf_task_context_get_node_id(payload_->ctx, &node_id);
    if (rc != TF_OK) {
      Napi::Error::New(env, tf_error_message(rc)).ThrowAsJavaScriptException();
      return env.Null();
    }
    return Napi::BigInt::New(env, node_id);
  }

  Napi::Value Contains(const Napi::CallbackInfo& info);
  Napi::Value Get(const Napi::CallbackInfo& info);
  Napi::Value Set(const Napi::CallbackInfo& info);
  Napi::Value GetResult(const Napi::CallbackInfo& info);
  Napi::Value SetResult(const Napi::CallbackInfo& info);
  Napi::Value Progress(const Napi::CallbackInfo& info);
  Napi::Value ReportProgress(const Napi::CallbackInfo& info);
  Napi::Value IsCancelled(const Napi::CallbackInfo& info);
  Napi::Value Cancel(const Napi::CallbackInfo& info);
  Napi::Value SetError(const Napi::CallbackInfo& info);

  TaskContextPayload* payload_ = nullptr;
};

Napi::FunctionReference TaskContextProxy::constructor;

struct JsTaskRegistration {
  explicit JsTaskRegistration(Napi::ThreadSafeFunction fn) : tsfn(std::move(fn)) {}
  Napi::ThreadSafeFunction tsfn;
};

struct JsConditionRegistration {
  explicit JsConditionRegistration(Napi::ThreadSafeFunction fn) : tsfn(std::move(fn)) {}
  Napi::ThreadSafeFunction tsfn;
};

struct OrchestratorBindingState {
  std::vector<std::shared_ptr<JsTaskRegistration>> task_registrations;
};

struct BlueprintBindingState {
  std::vector<std::shared_ptr<JsConditionRegistration>> condition_registrations;
};

std::mutex g_binding_mutex;
std::unordered_map<uint64_t, std::shared_ptr<OrchestratorBindingState>> g_orchestrator_bindings;
std::unordered_map<uint64_t, std::shared_ptr<BlueprintBindingState>> g_blueprint_bindings;

uint64_t ReadUint64(Napi::Env env, const Napi::Value& value, const char* name) {
  if (value.IsBigInt()) {
    bool lossless = false;
    uint64_t parsed = value.As<Napi::BigInt>().Uint64Value(&lossless);
    if (!lossless) {
      Napi::TypeError::New(env, std::string(name) + " must fit into an unsigned 64-bit integer")
          .ThrowAsJavaScriptException();
      return 0;
    }
    return parsed;
  }

  if (value.IsNumber()) {
    double number = value.As<Napi::Number>().DoubleValue();
    if (!std::isfinite(number) || number < 0 || std::floor(number) != number ||
        number > static_cast<double>(std::numeric_limits<uint64_t>::max())) {
      Napi::TypeError::New(env, std::string(name) + " must be a non-negative integer").ThrowAsJavaScriptException();
      return 0;
    }
    return static_cast<uint64_t>(number);
  }

  Napi::TypeError::New(env, std::string(name) + " must be a number or bigint").ThrowAsJavaScriptException();
  return 0;
}

int64_t ReadInt64(Napi::Env env, const Napi::Value& value, const char* name) {
  if (value.IsBigInt()) {
    bool lossless = false;
    int64_t parsed = value.As<Napi::BigInt>().Int64Value(&lossless);
    if (!lossless) {
      Napi::TypeError::New(env, std::string(name) + " must fit into a signed 64-bit integer")
          .ThrowAsJavaScriptException();
      return 0;
    }
    return parsed;
  }

  if (value.IsNumber()) {
    double number = value.As<Napi::Number>().DoubleValue();
    if (!std::isfinite(number) || std::floor(number) != number ||
        number < static_cast<double>(std::numeric_limits<int64_t>::min()) ||
        number > static_cast<double>(std::numeric_limits<int64_t>::max())) {
      Napi::TypeError::New(env, std::string(name) + " must be a signed integer").ThrowAsJavaScriptException();
      return 0;
    }
    return static_cast<int64_t>(number);
  }

  Napi::TypeError::New(env, std::string(name) + " must be a number or bigint").ThrowAsJavaScriptException();
  return 0;
}

Napi::Value Uint64ToJs(Napi::Env env, uint64_t value) { return Napi::BigInt::New(env, value); }

bool EnsureTaskflowOk(Napi::Env env, int rc, const char* action) {
  if (rc == TF_OK) {
    return true;
  }

  std::string message = action ? std::string(action) + ": " + tf_error_message(rc) : tf_error_message(rc);
  Napi::Error::New(env, message).ThrowAsJavaScriptException();
  return false;
}

bool IsLookupMiss(int rc) { return rc == TF_ERROR_VALUE_NOT_FOUND || rc == TF_ERROR_TYPE_MISMATCH; }

struct GenericValueReader {
  std::function<int(char**, size_t*)> get_string;
  std::function<int(int*)> get_bool;
  std::function<int(uint64_t*)> get_uint64;
  std::function<int(int64_t*)> get_int64;
  std::function<int(double*)> get_double;
};

Napi::Value ReadGenericValue(Napi::Env env, const GenericValueReader& reader) {
  if (reader.get_string) {
    char* buffer = nullptr;
    size_t size = 0;
    const int rc = reader.get_string(&buffer, &size);
    if (rc == TF_OK) {
      Napi::String value = Napi::String::New(env, buffer, size);
      tf_string_free(buffer);
      return value;
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      EnsureTaskflowOk(env, rc, "read string value");
      return env.Null();
    }
  }

  if (reader.get_bool) {
    int value = 0;
    const int rc = reader.get_bool(&value);
    if (rc == TF_OK) {
      return Napi::Boolean::New(env, value != 0);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      EnsureTaskflowOk(env, rc, "read bool value");
      return env.Null();
    }
  }

  if (reader.get_uint64) {
    uint64_t value = 0;
    const int rc = reader.get_uint64(&value);
    if (rc == TF_OK) {
      return Napi::BigInt::New(env, value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      EnsureTaskflowOk(env, rc, "read uint64 value");
      return env.Null();
    }
  }

  if (reader.get_int64) {
    int64_t value = 0;
    const int rc = reader.get_int64(&value);
    if (rc == TF_OK) {
      return Napi::BigInt::New(env, value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      EnsureTaskflowOk(env, rc, "read int64 value");
      return env.Null();
    }
  }

  if (reader.get_double) {
    double value = 0.0;
    const int rc = reader.get_double(&value);
    if (rc == TF_OK) {
      return Napi::Number::New(env, value);
    }
    if (rc != TF_ERROR_NOT_SUPPORTED && !IsLookupMiss(rc)) {
      EnsureTaskflowOk(env, rc, "read double value");
      return env.Null();
    }
  }

  return env.Undefined();
}

struct GenericValueWriter {
  std::function<int(int)> set_bool;
  std::function<int(int64_t)> set_int64;
  std::function<int(uint64_t)> set_uint64;
  std::function<int(double)> set_double;
  std::function<int(const char*)> set_string;
};

bool WriteGenericValue(Napi::Env env, const Napi::Value& value, const GenericValueWriter& writer, const char* name) {
  int rc = TF_ERROR_INVALID_ARGUMENT;

  if (value.IsBoolean()) {
    rc = writer.set_bool ? writer.set_bool(value.ToBoolean().Value() ? 1 : 0) : TF_ERROR_NOT_SUPPORTED;
  } else if (value.IsBigInt()) {
    bool lossless = false;
    int64_t signed_value = value.As<Napi::BigInt>().Int64Value(&lossless);
    if (lossless && writer.set_int64) {
      rc = writer.set_int64(signed_value);
    } else {
      uint64_t unsigned_value = value.As<Napi::BigInt>().Uint64Value(&lossless);
      if (!lossless || !writer.set_uint64) {
        Napi::TypeError::New(env, std::string(name) + " bigint is out of range for the native API")
            .ThrowAsJavaScriptException();
        return false;
      }
      rc = writer.set_uint64(unsigned_value);
    }
  } else if (value.IsNumber()) {
    double number = value.As<Napi::Number>().DoubleValue();
    if (!std::isfinite(number)) {
      Napi::TypeError::New(env, std::string(name) + " must be finite").ThrowAsJavaScriptException();
      return false;
    }

    if (std::floor(number) == number && number >= static_cast<double>(std::numeric_limits<int64_t>::min()) &&
        number <= static_cast<double>(std::numeric_limits<int64_t>::max()) && writer.set_int64) {
      rc = writer.set_int64(static_cast<int64_t>(number));
    } else {
      rc = writer.set_double ? writer.set_double(number) : TF_ERROR_NOT_SUPPORTED;
    }
  } else if (value.IsString()) {
    std::string string_value = value.As<Napi::String>().Utf8Value();
    rc = writer.set_string ? writer.set_string(string_value.c_str()) : TF_ERROR_NOT_SUPPORTED;
  } else if (value.IsNull() || value.IsUndefined()) {
    rc = TF_ERROR_INVALID_ARGUMENT;
  } else {
    Napi::TypeError::New(env, std::string(name) + " must be a boolean, number, bigint, or string")
        .ThrowAsJavaScriptException();
    return false;
  }

  return EnsureTaskflowOk(env, rc, "write native value");
}

bool ParseRunOptions(Napi::Env env, const Napi::Value& value, TF_RunOptions* out_options) {
  tf_run_options_init(out_options);
  if (value.IsUndefined() || value.IsNull()) {
    return true;
  }
  if (!value.IsObject()) {
    Napi::TypeError::New(env, "options must be an object when provided").ThrowAsJavaScriptException();
    return false;
  }

  Napi::Object options = value.As<Napi::Object>();
  if (options.Has("stopOnFirstFailure")) {
    out_options->stop_on_first_failure = options.Get("stopOnFirstFailure").ToBoolean().Value() ? 1 : 0;
  }
  if (options.Has("compensateOnFailure")) {
    out_options->compensate_on_failure = options.Get("compensateOnFailure").ToBoolean().Value() ? 1 : 0;
  }
  if (options.Has("compensateOnCancel")) {
    out_options->compensate_on_cancel = options.Get("compensateOnCancel").ToBoolean().Value() ? 1 : 0;
  }
  return true;
}

bool ParseOrchestratorOptions(Napi::Env env, const Napi::Value& value, TF_OrchestratorOptions* out_options) {
  tf_orchestrator_options_init(out_options);
  if (value.IsUndefined() || value.IsNull()) {
    return true;
  }
  if (!value.IsObject()) {
    Napi::TypeError::New(env, "orchestrator options must be an object when provided").ThrowAsJavaScriptException();
    return false;
  }

  Napi::Object options = value.As<Napi::Object>();
  if (options.Has("memoryStateStorage")) {
    out_options->enable_memory_state_storage = options.Get("memoryStateStorage").ToBoolean().Value() ? 1 : 0;
  }
  if (options.Has("memoryResultStorage")) {
    out_options->enable_memory_result_storage = options.Get("memoryResultStorage").ToBoolean().Value() ? 1 : 0;
  }
  return true;
}

bool ParseRetryPolicy(Napi::Env env, const Napi::Value& value, TF_RetryPolicy* out_policy) {
  if (!value.IsObject()) {
    Napi::TypeError::New(env, "retry policy must be an object").ThrowAsJavaScriptException();
    return false;
  }

  tf_retry_policy_init(out_policy);
  Napi::Object policy = value.As<Napi::Object>();

  if (policy.Has("maxAttempts")) {
    out_policy->max_attempts = policy.Get("maxAttempts").ToNumber().Int32Value();
  }
  if (policy.Has("initialDelayMs")) {
    out_policy->initial_delay_ms = ReadUint64(env, policy.Get("initialDelayMs"), "initialDelayMs");
    if (env.IsExceptionPending()) {
      return false;
    }
  }
  if (policy.Has("backoffMultiplier")) {
    out_policy->backoff_multiplier = policy.Get("backoffMultiplier").ToNumber().DoubleValue();
  }
  if (policy.Has("maxDelayMs")) {
    out_policy->max_delay_ms = ReadUint64(env, policy.Get("maxDelayMs"), "maxDelayMs");
    if (env.IsExceptionPending()) {
      return false;
    }
  }
  if (policy.Has("jitter")) {
    out_policy->jitter = policy.Get("jitter").ToBoolean().Value() ? 1 : 0;
  }
  if (policy.Has("jitterRangeMs")) {
    out_policy->jitter_range_ms = ReadUint64(env, policy.Get("jitterRangeMs"), "jitterRangeMs");
    if (env.IsExceptionPending()) {
      return false;
    }
  }

  return true;
}

bool NormalizeTaskReturn(Napi::Env env, const Napi::Value& value, int* out_state) {
  if (!out_state) {
    return false;
  }

  if (value.IsUndefined() || value.IsNull()) {
    *out_state = TF_TASK_STATE_SUCCESS;
    return true;
  }
  if (value.IsBoolean()) {
    *out_state = value.ToBoolean().Value() ? TF_TASK_STATE_SUCCESS : TF_TASK_STATE_FAILED;
    return true;
  }
  if (value.IsString()) {
    const std::string state = value.As<Napi::String>().Utf8Value();
    if (state == "success") {
      *out_state = TF_TASK_STATE_SUCCESS;
      return true;
    }
    if (state == "failed") {
      *out_state = TF_TASK_STATE_FAILED;
      return true;
    }
    if (state == "cancelled") {
      *out_state = TF_TASK_STATE_CANCELLED;
      return true;
    }
    if (state == "skipped") {
      *out_state = TF_TASK_STATE_SKIPPED;
      return true;
    }
    Napi::TypeError::New(env, "task callback string return value must be success, failed, cancelled, or skipped")
        .ThrowAsJavaScriptException();
    return false;
  }
  if (value.IsBigInt() || value.IsNumber()) {
    const int64_t state = ReadInt64(env, value, "task return state");
    if (env.IsExceptionPending()) {
      return false;
    }
    *out_state = static_cast<int>(state);
    return true;
  }

  Napi::TypeError::New(env, "task callback must return a task state, boolean, string, or undefined")
      .ThrowAsJavaScriptException();
  return false;
}

struct PendingTaskCall {
  tf_task_context_t ctx = nullptr;
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  int result = TF_TASK_STATE_FAILED;
  std::string error_message;
};

struct PendingConditionCall {
  tf_task_context_t ctx = nullptr;
  std::mutex mutex;
  std::condition_variable cv;
  bool done = false;
  bool result = false;
  std::string error_message;
};

int InvokeJsTaskCallback(JsTaskRegistration& registration, tf_task_context_t ctx) {
  PendingTaskCall call;
  call.ctx = ctx;

  const napi_status acquire_status = registration.tsfn.Acquire();
  if (acquire_status != napi_ok) {
    (void)tf_task_context_set_error(ctx, "failed to acquire JavaScript task callback");
    return TF_TASK_STATE_FAILED;
  }

  const napi_status call_status =
      registration.tsfn.BlockingCall(&call, [](Napi::Env env, Napi::Function js_callback, PendingTaskCall* pending) {
        Napi::HandleScope scope(env);

        TaskContextPayload* payload = new TaskContextPayload();
        payload->ctx = pending->ctx;
        Napi::Object ctx_object = TaskContextProxy::NewInstance(env, payload);

        Napi::Value result = js_callback.Call({ctx_object});
        if (env.IsExceptionPending()) {
          pending->error_message = env.GetAndClearPendingException().Message();
          pending->result = TF_TASK_STATE_FAILED;
        } else if (!NormalizeTaskReturn(env, result, &pending->result)) {
          if (env.IsExceptionPending()) {
            pending->error_message = env.GetAndClearPendingException().Message();
          } else {
            pending->error_message = "JavaScript task callback returned an unsupported value";
          }
          pending->result = TF_TASK_STATE_FAILED;
        }

        payload->alive->store(false);

        {
          std::lock_guard<std::mutex> lock(pending->mutex);
          pending->done = true;
        }
        pending->cv.notify_one();
      });

  if (call_status != napi_ok) {
    registration.tsfn.Release();
    (void)tf_task_context_set_error(ctx, "failed to schedule JavaScript task callback");
    return TF_TASK_STATE_FAILED;
  }

  {
    std::unique_lock<std::mutex> lock(call.mutex);
    call.cv.wait(lock, [&call]() { return call.done; });
  }

  registration.tsfn.Release();

  if (!call.error_message.empty()) {
    (void)tf_task_context_set_error(ctx, call.error_message.c_str());
  }

  return call.result;
}

bool InvokeJsConditionCallback(JsConditionRegistration& registration, tf_task_context_t ctx) {
  PendingConditionCall call;
  call.ctx = ctx;

  const napi_status acquire_status = registration.tsfn.Acquire();
  if (acquire_status != napi_ok) {
    return false;
  }

  const napi_status call_status = registration.tsfn.BlockingCall(
      &call, [](Napi::Env env, Napi::Function js_callback, PendingConditionCall* pending) {
        Napi::HandleScope scope(env);

        TaskContextPayload* payload = new TaskContextPayload();
        payload->ctx = pending->ctx;
        Napi::Object ctx_object = TaskContextProxy::NewInstance(env, payload);

        Napi::Value result = js_callback.Call({ctx_object});
        if (env.IsExceptionPending()) {
          pending->error_message = env.GetAndClearPendingException().Message();
          pending->result = false;
        } else {
          pending->result = result.ToBoolean().Value();
        }

        payload->alive->store(false);

        {
          std::lock_guard<std::mutex> lock(pending->mutex);
          pending->done = true;
        }
        pending->cv.notify_one();
      });

  if (call_status != napi_ok) {
    registration.tsfn.Release();
    return false;
  }

  {
    std::unique_lock<std::mutex> lock(call.mutex);
    call.cv.wait(lock, [&call]() { return call.done; });
  }

  registration.tsfn.Release();
  return call.result;
}

int JsTaskCallbackBridge(tf_task_context_t ctx, void* userdata) {
  auto* registration = static_cast<JsTaskRegistration*>(userdata);
  return registration ? InvokeJsTaskCallback(*registration, ctx) : TF_TASK_STATE_FAILED;
}

int JsConditionCallbackBridge(tf_task_context_t ctx, void* userdata) {
  auto* registration = static_cast<JsConditionRegistration*>(userdata);
  return registration && InvokeJsConditionCallback(*registration, ctx) ? 1 : 0;
}

void ReleaseTaskRegistrations(const std::shared_ptr<OrchestratorBindingState>& state) {
  if (!state) {
    return;
  }
  for (const auto& registration : state->task_registrations) {
    if (registration) {
      registration->tsfn.Abort();
      registration->tsfn.Release();
    }
  }
}

void ReleaseConditionRegistrations(const std::shared_ptr<BlueprintBindingState>& state) {
  if (!state) {
    return;
  }
  for (const auto& registration : state->condition_registrations) {
    if (registration) {
      registration->tsfn.Abort();
      registration->tsfn.Release();
    }
  }
}

std::shared_ptr<OrchestratorBindingState> FindOrchestratorBinding(tf_orchestrator_t handle) {
  std::lock_guard<std::mutex> lock(g_binding_mutex);
  auto it = g_orchestrator_bindings.find(handle);
  if (it == g_orchestrator_bindings.end()) {
    return {};
  }
  return it->second;
}

std::shared_ptr<BlueprintBindingState> FindBlueprintBinding(tf_blueprint_t handle) {
  std::lock_guard<std::mutex> lock(g_binding_mutex);
  auto it = g_blueprint_bindings.find(handle);
  if (it == g_blueprint_bindings.end()) {
    return {};
  }
  return it->second;
}

class WaitExecutionWorker : public Napi::AsyncWorker {
 public:
  WaitExecutionWorker(Napi::Env env, tf_execution_t handle)
      : Napi::AsyncWorker(env), deferred_(Napi::Promise::Deferred::New(env)), handle_(handle) {}

  void Execute() override {
    rc_ = tf_execution_wait(handle_, &state_);
    if (rc_ != TF_OK) {
      SetError(tf_error_message(rc_));
    }
  }

  void OnOK() override { deferred_.Resolve(Napi::Number::New(Env(), state_)); }

  void OnError(const Napi::Error& error) override { deferred_.Reject(error.Value()); }

  Napi::Promise QueueAndGetPromise() {
    Queue();
    return deferred_.Promise();
  }

 private:
  Napi::Promise::Deferred deferred_;
  tf_execution_t handle_;
  int rc_ = TF_OK;
  int state_ = TF_TASK_STATE_PENDING;
};

Napi::Value CreateOrchestrator(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();

  TF_OrchestratorOptions options{};
  TF_OrchestratorOptions* options_ptr = nullptr;
  if (info.Length() >= 1 && !info[0].IsUndefined() && !info[0].IsNull()) {
    if (!ParseOrchestratorOptions(env, info[0], &options)) {
      return env.Null();
    }
    options_ptr = &options;
  }

  tf_orchestrator_t handle = 0;
  const int rc = tf_orchestrator_create_ex(options_ptr, &handle);
  if (!EnsureTaskflowOk(env, rc, "create orchestrator")) {
    return env.Null();
  }

  auto binding_state = std::make_shared<OrchestratorBindingState>();
  {
    std::lock_guard<std::mutex> lock(g_binding_mutex);
    g_orchestrator_bindings.emplace(handle, binding_state);
  }
  return Uint64ToJs(env, handle);
}

Napi::Value DestroyOrchestrator(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected orchestrator handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_orchestrator_destroy(handle);
  if (!EnsureTaskflowOk(env, rc, "destroy orchestrator")) {
    return env.Null();
  }

  std::shared_ptr<OrchestratorBindingState> state;
  {
    std::lock_guard<std::mutex> lock(g_binding_mutex);
    auto it = g_orchestrator_bindings.find(handle);
    if (it != g_orchestrator_bindings.end()) {
      state = it->second;
      g_orchestrator_bindings.erase(it);
    }
  }
  ReleaseTaskRegistrations(state);
  return env.Undefined();
}

Napi::Value RegisterTask(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[1].IsString() || !info[2].IsFunction()) {
    Napi::TypeError::New(env, "Expected (handle, taskType, callback)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  auto state = FindOrchestratorBinding(handle);
  if (!state) {
    Napi::Error::New(env, "Unknown orchestrator handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  std::string task_type = info[1].As<Napi::String>().Utf8Value();
  Napi::Function callback = info[2].As<Napi::Function>();

  auto registration =
      std::make_shared<JsTaskRegistration>(Napi::ThreadSafeFunction::New(env, callback, "TaskFlowTaskCallback", 0, 1));

  const int rc =
      tf_orchestrator_register_task_callback(handle, task_type.c_str(), JsTaskCallbackBridge, registration.get());
  if (!EnsureTaskflowOk(env, rc, "register task")) {
    registration->tsfn.Abort();
    registration->tsfn.Release();
    return env.Null();
  }

  state->task_registrations.push_back(registration);
  return env.Undefined();
}

Napi::Value HasTask(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, taskType)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int has_task = 0;
  const int rc = tf_orchestrator_has_task(handle, info[1].As<Napi::String>().Utf8Value().c_str(), &has_task);
  if (!EnsureTaskflowOk(env, rc, "query task")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, has_task != 0);
}

Napi::Value CreateBlueprint(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  tf_blueprint_t handle = 0;
  const int rc = tf_blueprint_create(&handle);
  if (!EnsureTaskflowOk(env, rc, "create blueprint")) {
    return env.Null();
  }

  auto binding_state = std::make_shared<BlueprintBindingState>();
  {
    std::lock_guard<std::mutex> lock(g_binding_mutex);
    g_blueprint_bindings.emplace(handle, binding_state);
  }
  return Uint64ToJs(env, handle);
}

Napi::Value DestroyBlueprint(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected blueprint handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_blueprint_destroy(handle);
  if (!EnsureTaskflowOk(env, rc, "destroy blueprint")) {
    return env.Null();
  }

  std::shared_ptr<BlueprintBindingState> state;
  {
    std::lock_guard<std::mutex> lock(g_binding_mutex);
    auto it = g_blueprint_bindings.find(handle);
    if (it != g_blueprint_bindings.end()) {
      state = it->second;
      g_blueprint_bindings.erase(it);
    }
  }
  ReleaseConditionRegistrations(state);
  return env.Undefined();
}

Napi::Value BlueprintAddNode(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[2].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, nodeId, taskType, label?)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t node_id = ReadUint64(env, info[1], "nodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  std::string task_type = info[2].As<Napi::String>().Utf8Value();
  const char* label = nullptr;
  std::string label_storage;
  if (info.Length() >= 4 && !info[3].IsUndefined() && !info[3].IsNull()) {
    if (!info[3].IsString()) {
      Napi::TypeError::New(env, "label must be a string when provided").ThrowAsJavaScriptException();
      return env.Null();
    }
    label_storage = info[3].As<Napi::String>().Utf8Value();
    label = label_storage.c_str();
  }

  const int rc = tf_blueprint_add_node(handle, node_id, task_type.c_str(), label);
  if (!EnsureTaskflowOk(env, rc, "add blueprint node")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value BlueprintSetNodeRetry(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3) {
    Napi::TypeError::New(env, "Expected (handle, nodeId, retry|null)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t node_id = ReadUint64(env, info[1], "nodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RetryPolicy retry{};
  TF_RetryPolicy* retry_ptr = nullptr;
  if (!info[2].IsNull() && !info[2].IsUndefined()) {
    if (!ParseRetryPolicy(env, info[2], &retry)) {
      return env.Null();
    }
    retry_ptr = &retry;
  }

  const int rc = tf_blueprint_set_node_retry(handle, node_id, retry_ptr);
  if (!EnsureTaskflowOk(env, rc, "set node retry")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value BlueprintSetNodeCompensation(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || (!info[2].IsString() && !info[2].IsNull() && !info[2].IsUndefined())) {
    Napi::TypeError::New(env, "Expected (handle, nodeId, compensateTaskType|null, compensateRetry?)")
        .ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t node_id = ReadUint64(env, info[1], "nodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const char* compensate_task_type = nullptr;
  std::string compensate_task_type_storage;
  if (info[2].IsString()) {
    compensate_task_type_storage = info[2].As<Napi::String>().Utf8Value();
    compensate_task_type = compensate_task_type_storage.c_str();
  }

  TF_RetryPolicy retry{};
  TF_RetryPolicy* retry_ptr = nullptr;
  if (info.Length() >= 4 && !info[3].IsNull() && !info[3].IsUndefined()) {
    if (!ParseRetryPolicy(env, info[3], &retry)) {
      return env.Null();
    }
    retry_ptr = &retry;
  }

  const int rc = tf_blueprint_set_node_compensation(handle, node_id, compensate_task_type, retry_ptr);
  if (!EnsureTaskflowOk(env, rc, "set node compensation")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value BlueprintAddNodeTag(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[2].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, nodeId, tag)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t node_id = ReadUint64(env, info[1], "nodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string tag = info[2].As<Napi::String>().Utf8Value();
  const int rc = tf_blueprint_add_node_tag(handle, node_id, tag.c_str());
  if (!EnsureTaskflowOk(env, rc, "add node tag")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value BlueprintAddEdge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3) {
    Napi::TypeError::New(env, "Expected (handle, fromNodeId, toNodeId)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t from_node_id = ReadUint64(env, info[1], "fromNodeId");
  const uint64_t to_node_id = ReadUint64(env, info[2], "toNodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_blueprint_add_edge(handle, from_node_id, to_node_id);
  if (!EnsureTaskflowOk(env, rc, "add blueprint edge")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value BlueprintAddConditionalEdge(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 4 || !info[3].IsFunction()) {
    Napi::TypeError::New(env, "Expected (handle, fromNodeId, toNodeId, callback)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t from_node_id = ReadUint64(env, info[1], "fromNodeId");
  const uint64_t to_node_id = ReadUint64(env, info[2], "toNodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  auto state = FindBlueprintBinding(handle);
  if (!state) {
    Napi::Error::New(env, "Unknown blueprint handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  auto registration = std::make_shared<JsConditionRegistration>(
      Napi::ThreadSafeFunction::New(env, info[3].As<Napi::Function>(), "TaskFlowConditionCallback", 0, 1));

  const int rc = tf_blueprint_add_conditional_edge(handle, from_node_id, to_node_id, JsConditionCallbackBridge,
                                                   registration.get());
  if (!EnsureTaskflowOk(env, rc, "add conditional edge")) {
    registration->tsfn.Abort();
    registration->tsfn.Release();
    return env.Null();
  }

  state->condition_registrations.push_back(registration);
  return env.Undefined();
}

Napi::Value BlueprintToJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected blueprint handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  char* json = nullptr;
  size_t json_size = 0;
  const int rc = tf_blueprint_to_json_copy(handle, &json, &json_size);
  if (!EnsureTaskflowOk(env, rc, "serialize blueprint")) {
    return env.Null();
  }

  Napi::String result = Napi::String::New(env, json, json_size);
  tf_string_free(json);
  return result;
}

Napi::Value BlueprintValidate(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected blueprint handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  char* message = nullptr;
  size_t size = 0;
  const int rc = tf_blueprint_validate_copy(handle, &message, &size);
  if (!EnsureTaskflowOk(env, rc, "validate blueprint")) {
    return env.Null();
  }

  Napi::String result = Napi::String::New(env, message, size);
  tf_string_free(message);
  return result;
}

Napi::Value BlueprintIsValid(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected blueprint handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_blueprint_t handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int is_valid = 0;
  const int rc = tf_blueprint_is_valid(handle, &is_valid);
  if (!EnsureTaskflowOk(env, rc, "check blueprint validity")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, is_valid != 0);
}

Napi::Value RegisterBlueprint(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId, blueprintHandle)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  const tf_blueprint_t blueprint_handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[2], "blueprintHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_orchestrator_register_blueprint(handle, blueprint_id, blueprint_handle);
  if (!EnsureTaskflowOk(env, rc, "register blueprint")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value RegisterBlueprintName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName, blueprintHandle)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const tf_blueprint_t blueprint_handle = static_cast<tf_blueprint_t>(ReadUint64(env, info[2], "blueprintHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_orchestrator_register_blueprint_name(handle, blueprint_name.c_str(), blueprint_handle);
  if (!EnsureTaskflowOk(env, rc, "register named blueprint")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value RegisterBlueprintJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[2].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId, json)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string json = info[2].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_register_blueprint_json(handle, blueprint_id, json.c_str());
  if (!EnsureTaskflowOk(env, rc, "register blueprint json")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value RegisterBlueprintNameJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[1].IsString() || !info[2].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName, json)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const std::string json = info[2].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_register_blueprint_name_json(handle, blueprint_name.c_str(), json.c_str());
  if (!EnsureTaskflowOk(env, rc, "register named blueprint json")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value HasBlueprint(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int has_blueprint = 0;
  const int rc = tf_orchestrator_has_blueprint(handle, blueprint_id, &has_blueprint);
  if (!EnsureTaskflowOk(env, rc, "query blueprint")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, has_blueprint != 0);
}

Napi::Value HasBlueprintName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int has_blueprint = 0;
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_has_blueprint_name(handle, blueprint_name.c_str(), &has_blueprint);
  if (!EnsureTaskflowOk(env, rc, "query named blueprint")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, has_blueprint != 0);
}

Napi::Value GetBlueprintJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  char* json = nullptr;
  size_t size = 0;
  const int rc = tf_orchestrator_get_blueprint_json_copy(handle, blueprint_id, &json, &size);
  if (!EnsureTaskflowOk(env, rc, "get blueprint json")) {
    return env.Null();
  }

  Napi::String result = Napi::String::New(env, json, size);
  tf_string_free(json);
  return result;
}

Napi::Value GetBlueprintNameJSON(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  char* json = nullptr;
  size_t size = 0;
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_get_blueprint_name_json_copy(handle, blueprint_name.c_str(), &json, &size);
  if (!EnsureTaskflowOk(env, rc, "get named blueprint json")) {
    return env.Null();
  }

  Napi::String result = Napi::String::New(env, json, size);
  tf_string_free(json);
  return result;
}

Napi::Value CreateExecution(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const int rc = tf_orchestrator_create_execution(handle, blueprint_id, &execution);
  if (!EnsureTaskflowOk(env, rc, "create execution")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value CreateExecutionName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_create_execution_name(handle, blueprint_name.c_str(), &execution);
  if (!EnsureTaskflowOk(env, rc, "create named execution")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value RunSync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId, options?)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RunOptions options{};
  if (!ParseRunOptions(env, info.Length() >= 3 ? info[2] : env.Null(), &options)) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const int rc = tf_orchestrator_run_sync(handle, blueprint_id, &options, &execution);
  if (!EnsureTaskflowOk(env, rc, "run workflow sync")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value RunSyncName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName, options?)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RunOptions options{};
  if (!ParseRunOptions(env, info.Length() >= 3 ? info[2] : env.Null(), &options)) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_run_sync_name(handle, blueprint_name.c_str(), &options, &execution);
  if (!EnsureTaskflowOk(env, rc, "run named workflow sync")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value RunAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (handle, blueprintId, options?)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  const uint64_t blueprint_id = ReadUint64(env, info[1], "blueprintId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RunOptions options{};
  if (!ParseRunOptions(env, info.Length() >= 3 ? info[2] : env.Null(), &options)) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const int rc = tf_orchestrator_run_async(handle, blueprint_id, &options, &execution);
  if (!EnsureTaskflowOk(env, rc, "run workflow async")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value RunAsyncName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (handle, blueprintName, options?)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RunOptions options{};
  if (!ParseRunOptions(env, info.Length() >= 3 ? info[2] : env.Null(), &options)) {
    return env.Null();
  }

  tf_execution_t execution = 0;
  const std::string blueprint_name = info[1].As<Napi::String>().Utf8Value();
  const int rc = tf_orchestrator_run_async_name(handle, blueprint_name.c_str(), &options, &execution);
  if (!EnsureTaskflowOk(env, rc, "run named workflow async")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution);
}

Napi::Value CleanupCompletedExecutions(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected orchestrator handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_orchestrator_t handle = static_cast<tf_orchestrator_t>(ReadUint64(env, info[0], "handle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  size_t count = 0;
  const int rc = tf_orchestrator_cleanup_completed_executions(handle, &count);
  if (!EnsureTaskflowOk(env, rc, "cleanup completed executions")) {
    return env.Null();
  }
  return Napi::Number::New(env, static_cast<double>(count));
}

Napi::Value ExecutionRunSync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  TF_RunOptions options{};
  if (!ParseRunOptions(env, info.Length() >= 2 ? info[1] : env.Null(), &options)) {
    return env.Null();
  }

  int state = TF_TASK_STATE_PENDING;
  const int rc = tf_execution_run_sync(handle, &options, &state);
  if (!EnsureTaskflowOk(env, rc, "run execution sync")) {
    return env.Null();
  }
  return Napi::Number::New(env, state);
}

Napi::Value WaitExecutionAsync(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  auto* worker = new WaitExecutionWorker(env, handle);
  return worker->QueueAndGetPromise();
}

Napi::Value PollExecution(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int is_ready = 0;
  int state = TF_TASK_STATE_PENDING;
  const int rc = tf_execution_poll(handle, &is_ready, &state);
  if (!EnsureTaskflowOk(env, rc, "poll execution")) {
    return env.Null();
  }

  Napi::Object result = Napi::Object::New(env);
  result.Set("ready", Napi::Boolean::New(env, is_ready != 0));
  result.Set("state", Napi::Number::New(env, state));
  result.Set("stateName", Napi::String::New(env, tf_task_state_name(state)));
  return result;
}

Napi::Value CancelExecution(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }
  const int rc = tf_execution_cancel(handle);
  if (!EnsureTaskflowOk(env, rc, "cancel execution")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value GetExecutionId(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  uint64_t execution_id = 0;
  const int rc = tf_execution_get_id(handle, &execution_id);
  if (!EnsureTaskflowOk(env, rc, "get execution id")) {
    return env.Null();
  }
  return Uint64ToJs(env, execution_id);
}

Napi::Value GetExecutionSnapshot(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  char* json = nullptr;
  size_t size = 0;
  const int rc = tf_execution_snapshot_json_copy(handle, &json, &size);
  if (!EnsureTaskflowOk(env, rc, "get execution snapshot")) {
    return env.Null();
  }

  Napi::String result = Napi::String::New(env, json, size);
  tf_string_free(json);
  return result;
}

Napi::Value GetOverallState(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int state = TF_TASK_STATE_PENDING;
  const int rc = tf_execution_get_overall_state(handle, &state);
  if (!EnsureTaskflowOk(env, rc, "get overall state")) {
    return env.Null();
  }
  return Napi::Number::New(env, state);
}

Napi::Value GetNodeState(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2) {
    Napi::TypeError::New(env, "Expected (executionHandle, nodeId)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  const uint64_t node_id = ReadUint64(env, info[1], "nodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int state = TF_TASK_STATE_PENDING;
  const int rc = tf_execution_get_node_state(handle, node_id, &state);
  if (!EnsureTaskflowOk(env, rc, "get node state")) {
    return env.Null();
  }
  return Napi::Number::New(env, state);
}

Napi::Value IsExecutionComplete(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int is_complete = 0;
  const int rc = tf_execution_is_complete(handle, &is_complete);
  if (!EnsureTaskflowOk(env, rc, "check execution completion")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, is_complete != 0);
}

Napi::Value IsExecutionCancelled(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  int is_cancelled = 0;
  const int rc = tf_execution_is_cancelled(handle, &is_cancelled);
  if (!EnsureTaskflowOk(env, rc, "check execution cancellation")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, is_cancelled != 0);
}

Napi::Value SetExecutionContextValue(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (executionHandle, key, value)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string key = info[1].As<Napi::String>().Utf8Value();
  const bool ok = WriteGenericValue(
      env, info[2],
      GenericValueWriter{
          [&](int value) { return tf_execution_context_set_bool(handle, key.c_str(), value); },
          [&](int64_t value) { return tf_execution_context_set_int64(handle, key.c_str(), value); },
          [&](uint64_t value) { return tf_execution_context_set_uint64(handle, key.c_str(), value); },
          [&](double value) { return tf_execution_context_set_double(handle, key.c_str(), value); },
          [&](const char* value) { return tf_execution_context_set_string(handle, key.c_str(), value); },
      },
      "execution context value");

  return ok ? env.Undefined() : env.Null();
}

Napi::Value GetExecutionContextValue(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (executionHandle, key)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string key = info[1].As<Napi::String>().Utf8Value();
  return ReadGenericValue(
      env, GenericValueReader{
               [&](char** buffer, size_t* size) {
                 return tf_execution_context_get_string_copy(handle, key.c_str(), buffer, size);
               },
               [&](int* value) { return tf_execution_context_get_bool(handle, key.c_str(), value); },
               [&](uint64_t* value) { return tf_execution_context_get_uint64(handle, key.c_str(), value); },
               [&](int64_t* value) { return tf_execution_context_get_int64(handle, key.c_str(), value); },
               [&](double* value) { return tf_execution_context_get_double(handle, key.c_str(), value); },
           });
}

Napi::Value GetExecutionResultValue(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 3 || !info[2].IsString()) {
    Napi::TypeError::New(env, "Expected (executionHandle, fromNodeId, key)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  const uint64_t from_node_id = ReadUint64(env, info[1], "fromNodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const std::string key = info[2].As<Napi::String>().Utf8Value();
  return ReadGenericValue(
      env,
      GenericValueReader{
          [&](char** buffer, size_t* size) {
            return tf_execution_result_get_string_copy(handle, from_node_id, key.c_str(), buffer, size);
          },
          [&](int* value) { return tf_execution_result_get_bool(handle, from_node_id, key.c_str(), value); },
          [&](uint64_t* value) { return tf_execution_result_get_uint64(handle, from_node_id, key.c_str(), value); },
          [&](int64_t* value) { return tf_execution_result_get_int64(handle, from_node_id, key.c_str(), value); },
          [&](double* value) { return tf_execution_result_get_double(handle, from_node_id, key.c_str(), value); },
      });
}

Napi::Value DestroyExecution(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1) {
    Napi::TypeError::New(env, "Expected execution handle").ThrowAsJavaScriptException();
    return env.Null();
  }
  const tf_execution_t handle = static_cast<tf_execution_t>(ReadUint64(env, info[0], "executionHandle"));
  if (env.IsExceptionPending()) {
    return env.Null();
  }

  const int rc = tf_execution_destroy(handle);
  if (!EnsureTaskflowOk(env, rc, "destroy execution")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value ErrorMessage(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected error code number").ThrowAsJavaScriptException();
    return env.Null();
  }
  return Napi::String::New(env, tf_error_message(info[0].As<Napi::Number>().Int32Value()));
}

Napi::Value TaskStateName(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected task state number").ThrowAsJavaScriptException();
    return env.Null();
  }
  return Napi::String::New(env, tf_task_state_name(info[0].As<Napi::Number>().Int32Value()));
}

Napi::Value TaskContextProxy::Contains(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected key string").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string key = info[0].As<Napi::String>().Utf8Value();
  int contains = 0;
  const int rc = tf_task_context_contains(payload_->ctx, key.c_str(), &contains);
  if (!EnsureTaskflowOk(env, rc, "check task context key")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, contains != 0);
}

Napi::Value TaskContextProxy::Get(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected key string").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string key = info[0].As<Napi::String>().Utf8Value();
  return ReadGenericValue(
      env, GenericValueReader{
               [&](char** buffer, size_t* size) {
                 return tf_task_context_get_string_copy(payload_->ctx, key.c_str(), buffer, size);
               },
               [&](int* value) { return tf_task_context_get_bool(payload_->ctx, key.c_str(), value); },
               [&](uint64_t* value) { return tf_task_context_get_uint64(payload_->ctx, key.c_str(), value); },
               [&](int64_t* value) { return tf_task_context_get_int64(payload_->ctx, key.c_str(), value); },
               [&](double* value) { return tf_task_context_get_double(payload_->ctx, key.c_str(), value); },
           });
}

Napi::Value TaskContextProxy::Set(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 2 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected (key, value)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string key = info[0].As<Napi::String>().Utf8Value();
  const bool ok = WriteGenericValue(
      env, info[1],
      GenericValueWriter{
          [&](int value) { return tf_task_context_set_bool(payload_->ctx, key.c_str(), value); },
          [&](int64_t value) { return tf_task_context_set_int64(payload_->ctx, key.c_str(), value); },
          [&](uint64_t value) { return tf_task_context_set_uint64(payload_->ctx, key.c_str(), value); },
          [&](double value) { return tf_task_context_set_double(payload_->ctx, key.c_str(), value); },
          [&](const char* value) { return tf_task_context_set_string(payload_->ctx, key.c_str(), value); },
      },
      "task context value");
  return ok ? env.Undefined() : env.Null();
}

Napi::Value TaskContextProxy::GetResult(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 2 || !info[1].IsString()) {
    Napi::TypeError::New(env, "Expected (fromNodeId, key)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const uint64_t from_node_id = ReadUint64(env, info[0], "fromNodeId");
  if (env.IsExceptionPending()) {
    return env.Null();
  }
  const std::string key = info[1].As<Napi::String>().Utf8Value();
  return ReadGenericValue(
      env,
      GenericValueReader{
          [&](char** buffer, size_t* size) {
            return tf_task_context_get_result_string_copy(payload_->ctx, from_node_id, key.c_str(), buffer, size);
          },
          [&](int* value) { return tf_task_context_get_result_bool(payload_->ctx, from_node_id, key.c_str(), value); },
          [&](uint64_t* value) {
            return tf_task_context_get_result_uint64(payload_->ctx, from_node_id, key.c_str(), value);
          },
          [&](int64_t* value) {
            return tf_task_context_get_result_int64(payload_->ctx, from_node_id, key.c_str(), value);
          },
          [&](double* value) {
            return tf_task_context_get_result_double(payload_->ctx, from_node_id, key.c_str(), value);
          },
      });
}

Napi::Value TaskContextProxy::SetResult(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 2 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected (key, value)").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string key = info[0].As<Napi::String>().Utf8Value();
  const bool ok = WriteGenericValue(
      env, info[1],
      GenericValueWriter{
          [&](int value) { return tf_task_context_set_result_bool(payload_->ctx, key.c_str(), value); },
          [&](int64_t value) { return tf_task_context_set_result_int64(payload_->ctx, key.c_str(), value); },
          [&](uint64_t value) { return tf_task_context_set_result_uint64(payload_->ctx, key.c_str(), value); },
          [&](double value) { return tf_task_context_set_result_double(payload_->ctx, key.c_str(), value); },
          [&](const char* value) { return tf_task_context_set_result_string(payload_->ctx, key.c_str(), value); },
      },
      "task result value");
  return ok ? env.Undefined() : env.Null();
}

Napi::Value TaskContextProxy::Progress(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }

  double progress = 0.0;
  const int rc = tf_task_context_get_progress(payload_->ctx, &progress);
  if (!EnsureTaskflowOk(env, rc, "get task progress")) {
    return env.Null();
  }
  return Napi::Number::New(env, progress);
}

Napi::Value TaskContextProxy::ReportProgress(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 1 || !info[0].IsNumber()) {
    Napi::TypeError::New(env, "Expected progress number").ThrowAsJavaScriptException();
    return env.Null();
  }

  const int rc = tf_task_context_report_progress(payload_->ctx, info[0].As<Napi::Number>().DoubleValue());
  if (!EnsureTaskflowOk(env, rc, "report task progress")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value TaskContextProxy::IsCancelled(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }

  int is_cancelled = 0;
  const int rc = tf_task_context_is_cancelled(payload_->ctx, &is_cancelled);
  if (!EnsureTaskflowOk(env, rc, "check task cancellation")) {
    return env.Null();
  }
  return Napi::Boolean::New(env, is_cancelled != 0);
}

Napi::Value TaskContextProxy::Cancel(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }

  const int rc = tf_task_context_request_cancel(payload_->ctx);
  if (!EnsureTaskflowOk(env, rc, "cancel task")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Value TaskContextProxy::SetError(const Napi::CallbackInfo& info) {
  Napi::Env env = info.Env();
  if (!EnsureAlive(env)) {
    return env.Null();
  }
  if (info.Length() < 1 || !info[0].IsString()) {
    Napi::TypeError::New(env, "Expected error string").ThrowAsJavaScriptException();
    return env.Null();
  }

  const std::string error = info[0].As<Napi::String>().Utf8Value();
  const int rc = tf_task_context_set_error(payload_->ctx, error.c_str());
  if (!EnsureTaskflowOk(env, rc, "set task error")) {
    return env.Null();
  }
  return env.Undefined();
}

Napi::Object Init(Napi::Env env, Napi::Object exports) {
  TaskContextProxy::Init(env, exports);

  exports.Set("createOrchestrator", Napi::Function::New(env, CreateOrchestrator));
  exports.Set("destroyOrchestrator", Napi::Function::New(env, DestroyOrchestrator));
  exports.Set("registerTask", Napi::Function::New(env, RegisterTask));
  exports.Set("hasTask", Napi::Function::New(env, HasTask));

  exports.Set("createBlueprint", Napi::Function::New(env, CreateBlueprint));
  exports.Set("destroyBlueprint", Napi::Function::New(env, DestroyBlueprint));
  exports.Set("blueprintAddNode", Napi::Function::New(env, BlueprintAddNode));
  exports.Set("blueprintSetNodeRetry", Napi::Function::New(env, BlueprintSetNodeRetry));
  exports.Set("blueprintSetNodeCompensation", Napi::Function::New(env, BlueprintSetNodeCompensation));
  exports.Set("blueprintAddNodeTag", Napi::Function::New(env, BlueprintAddNodeTag));
  exports.Set("blueprintAddEdge", Napi::Function::New(env, BlueprintAddEdge));
  exports.Set("blueprintAddConditionalEdge", Napi::Function::New(env, BlueprintAddConditionalEdge));
  exports.Set("blueprintToJSON", Napi::Function::New(env, BlueprintToJSON));
  exports.Set("blueprintValidate", Napi::Function::New(env, BlueprintValidate));
  exports.Set("blueprintIsValid", Napi::Function::New(env, BlueprintIsValid));

  exports.Set("registerBlueprint", Napi::Function::New(env, RegisterBlueprint));
  exports.Set("registerBlueprintName", Napi::Function::New(env, RegisterBlueprintName));
  exports.Set("registerBlueprintJSON", Napi::Function::New(env, RegisterBlueprintJSON));
  exports.Set("registerBlueprintNameJSON", Napi::Function::New(env, RegisterBlueprintNameJSON));
  exports.Set("hasBlueprint", Napi::Function::New(env, HasBlueprint));
  exports.Set("hasBlueprintName", Napi::Function::New(env, HasBlueprintName));
  exports.Set("getBlueprintJSON", Napi::Function::New(env, GetBlueprintJSON));
  exports.Set("getBlueprintNameJSON", Napi::Function::New(env, GetBlueprintNameJSON));

  exports.Set("createExecution", Napi::Function::New(env, CreateExecution));
  exports.Set("createExecutionName", Napi::Function::New(env, CreateExecutionName));
  exports.Set("runSync", Napi::Function::New(env, RunSync));
  exports.Set("runSyncName", Napi::Function::New(env, RunSyncName));
  exports.Set("runAsync", Napi::Function::New(env, RunAsync));
  exports.Set("runAsyncName", Napi::Function::New(env, RunAsyncName));
  exports.Set("cleanupCompletedExecutions", Napi::Function::New(env, CleanupCompletedExecutions));

  exports.Set("executionRunSync", Napi::Function::New(env, ExecutionRunSync));
  exports.Set("waitExecutionAsync", Napi::Function::New(env, WaitExecutionAsync));
  exports.Set("pollExecution", Napi::Function::New(env, PollExecution));
  exports.Set("cancelExecution", Napi::Function::New(env, CancelExecution));
  exports.Set("getExecutionId", Napi::Function::New(env, GetExecutionId));
  exports.Set("getExecutionSnapshot", Napi::Function::New(env, GetExecutionSnapshot));
  exports.Set("getOverallState", Napi::Function::New(env, GetOverallState));
  exports.Set("getNodeState", Napi::Function::New(env, GetNodeState));
  exports.Set("isExecutionComplete", Napi::Function::New(env, IsExecutionComplete));
  exports.Set("isExecutionCancelled", Napi::Function::New(env, IsExecutionCancelled));
  exports.Set("setExecutionContextValue", Napi::Function::New(env, SetExecutionContextValue));
  exports.Set("getExecutionContextValue", Napi::Function::New(env, GetExecutionContextValue));
  exports.Set("getExecutionResultValue", Napi::Function::New(env, GetExecutionResultValue));
  exports.Set("destroyExecution", Napi::Function::New(env, DestroyExecution));

  exports.Set("errorMessage", Napi::Function::New(env, ErrorMessage));
  exports.Set("taskStateName", Napi::Function::New(env, TaskStateName));

  exports.Set("TF_OK", Napi::Number::New(env, TF_OK));
  exports.Set("TF_ERROR_UNKNOWN", Napi::Number::New(env, TF_ERROR_UNKNOWN));
  exports.Set("TF_ERROR_INVALID_HANDLE", Napi::Number::New(env, TF_ERROR_INVALID_HANDLE));
  exports.Set("TF_ERROR_BLUEPRINT_NOT_FOUND", Napi::Number::New(env, TF_ERROR_BLUEPRINT_NOT_FOUND));
  exports.Set("TF_ERROR_EXECUTION_NOT_FOUND", Napi::Number::New(env, TF_ERROR_EXECUTION_NOT_FOUND));
  exports.Set("TF_ERROR_BUS_SHUTDOWN", Napi::Number::New(env, TF_ERROR_BUS_SHUTDOWN));
  exports.Set("TF_ERROR_INVALID_STATE", Napi::Number::New(env, TF_ERROR_INVALID_STATE));
  exports.Set("TF_ERROR_INVALID_ARGUMENT", Napi::Number::New(env, TF_ERROR_INVALID_ARGUMENT));
  exports.Set("TF_ERROR_TASK_TYPE_NOT_FOUND", Napi::Number::New(env, TF_ERROR_TASK_TYPE_NOT_FOUND));
  exports.Set("TF_ERROR_OPERATION_NOT_PERMITTED", Napi::Number::New(env, TF_ERROR_OPERATION_NOT_PERMITTED));
  exports.Set("TF_ERROR_STORAGE", Napi::Number::New(env, TF_ERROR_STORAGE));
  exports.Set("TF_ERROR_TYPE_MISMATCH", Napi::Number::New(env, TF_ERROR_TYPE_MISMATCH));
  exports.Set("TF_ERROR_VALUE_NOT_FOUND", Napi::Number::New(env, TF_ERROR_VALUE_NOT_FOUND));
  exports.Set("TF_ERROR_NOT_SUPPORTED", Napi::Number::New(env, TF_ERROR_NOT_SUPPORTED));

  exports.Set("TF_TASK_STATE_PENDING", Napi::Number::New(env, TF_TASK_STATE_PENDING));
  exports.Set("TF_TASK_STATE_RUNNING", Napi::Number::New(env, TF_TASK_STATE_RUNNING));
  exports.Set("TF_TASK_STATE_SUCCESS", Napi::Number::New(env, TF_TASK_STATE_SUCCESS));
  exports.Set("TF_TASK_STATE_FAILED", Napi::Number::New(env, TF_TASK_STATE_FAILED));
  exports.Set("TF_TASK_STATE_RETRY", Napi::Number::New(env, TF_TASK_STATE_RETRY));
  exports.Set("TF_TASK_STATE_SKIPPED", Napi::Number::New(env, TF_TASK_STATE_SKIPPED));
  exports.Set("TF_TASK_STATE_CANCELLED", Napi::Number::New(env, TF_TASK_STATE_CANCELLED));
  exports.Set("TF_TASK_STATE_COMPENSATING", Napi::Number::New(env, TF_TASK_STATE_COMPENSATING));
  exports.Set("TF_TASK_STATE_COMPENSATED", Napi::Number::New(env, TF_TASK_STATE_COMPENSATED));
  exports.Set("TF_TASK_STATE_COMPENSATION_FAILED", Napi::Number::New(env, TF_TASK_STATE_COMPENSATION_FAILED));

  return exports;
}

}  // namespace

NODE_API_MODULE(Taskflow, Init)
