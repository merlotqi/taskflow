#pragma once

#include <stddef.h>
#include <stdint.h>

#if defined(_WIN32) && defined(TASKFLOW_CAPI_SHARED)
#  if defined(TASKFLOW_CAPI_BUILD)
#    define TF_API __declspec(dllexport)
#  else
#    define TF_API __declspec(dllimport)
#  endif
#else
#  define TF_API
#endif

#define TF_CAPI_VERSION 2u

#ifdef __cplusplus
extern "C" {
#endif

typedef uint64_t tf_blueprint_t;
typedef uint64_t tf_orchestrator_t;
typedef uint64_t tf_execution_t;
typedef struct TF_TaskContextOpaque* tf_task_context_t;

enum TF_Error {
  TF_OK = 0,
  TF_ERROR_UNKNOWN = 1,
  TF_ERROR_INVALID_HANDLE = 2,
  TF_ERROR_BLUEPRINT_NOT_FOUND = 3,
  TF_ERROR_EXECUTION_NOT_FOUND = 4,
  TF_ERROR_BUS_SHUTDOWN = 5,
  TF_ERROR_INVALID_STATE = 6,
  TF_ERROR_INVALID_ARGUMENT = 7,
  TF_ERROR_TASK_TYPE_NOT_FOUND = 8,
  TF_ERROR_OPERATION_NOT_PERMITTED = 9,
  TF_ERROR_STORAGE = 10,
  TF_ERROR_TYPE_MISMATCH = 11,
  TF_ERROR_VALUE_NOT_FOUND = 12,
  TF_ERROR_NOT_SUPPORTED = 13
};

enum TF_TaskState {
  TF_TASK_STATE_PENDING = 0,
  TF_TASK_STATE_RUNNING = 1,
  TF_TASK_STATE_SUCCESS = 2,
  TF_TASK_STATE_FAILED = 3,
  TF_TASK_STATE_RETRY = 4,
  TF_TASK_STATE_SKIPPED = 5,
  TF_TASK_STATE_CANCELLED = 6,
  TF_TASK_STATE_COMPENSATING = 7,
  TF_TASK_STATE_COMPENSATED = 8,
  TF_TASK_STATE_COMPENSATION_FAILED = 9
};

enum TF_EventType {
  TF_EVENT_NODE_READY = 1,
  TF_EVENT_NODE_STARTED = 2,
  TF_EVENT_NODE_FINISHED = 3,
  TF_EVENT_WORKFLOW_FINISHED = 4,
  TF_EVENT_COMPENSATION_STARTED = 5,
  TF_EVENT_COMPENSATION_FINISHED = 6
};

struct TF_RunOptions {
  size_t struct_size;
  uint8_t stop_on_first_failure;
  uint8_t compensate_on_failure;
  uint8_t compensate_on_cancel;
};

struct TF_OrchestratorOptions {
  size_t struct_size;
  uint8_t enable_memory_state_storage;
  uint8_t enable_memory_result_storage;
};

struct TF_RetryPolicy {
  size_t struct_size;
  int32_t max_attempts;
  uint64_t initial_delay_ms;
  double backoff_multiplier;
  uint64_t max_delay_ms;
  uint8_t jitter;
  uint64_t jitter_range_ms;
};

typedef void (*tf_event_callback_fn)(
    uint64_t exec_id,
    uint64_t node_id,
    int event_type,
    int success,
    const char* task_type,
    void* userdata
);

typedef int (*tf_task_callback_fn)(tf_task_context_t ctx, void* userdata);
typedef int (*tf_edge_condition_callback_fn)(tf_task_context_t ctx, void* userdata);

TF_API const char* taskflow_version_string(void);
TF_API void tf_run_options_init(struct TF_RunOptions* options);
TF_API void tf_orchestrator_options_init(struct TF_OrchestratorOptions* options);
TF_API void tf_retry_policy_init(struct TF_RetryPolicy* policy);

TF_API int tf_blueprint_create(tf_blueprint_t* out_handle);
TF_API int tf_blueprint_destroy(tf_blueprint_t handle);
TF_API int tf_blueprint_from_json_copy(const char* json_content, tf_blueprint_t* out_handle);
TF_API int tf_blueprint_add_node(
    tf_blueprint_t handle,
    uint64_t node_id,
    const char* task_type,
    const char* label
);
TF_API int tf_blueprint_set_node_retry(
    tf_blueprint_t handle,
    uint64_t node_id,
    const struct TF_RetryPolicy* retry
);
TF_API int tf_blueprint_set_node_compensation(
    tf_blueprint_t handle,
    uint64_t node_id,
    const char* compensate_task_type,
    const struct TF_RetryPolicy* compensate_retry
);
TF_API int tf_blueprint_add_node_tag(
    tf_blueprint_t handle,
    uint64_t node_id,
    const char* tag
);
TF_API int tf_blueprint_add_edge(
    tf_blueprint_t handle,
    uint64_t from_node_id,
    uint64_t to_node_id
);
TF_API int tf_blueprint_add_conditional_edge(
    tf_blueprint_t handle,
    uint64_t from_node_id,
    uint64_t to_node_id,
    tf_edge_condition_callback_fn callback,
    void* userdata
);
TF_API int tf_blueprint_has_node(
    tf_blueprint_t handle,
    uint64_t node_id,
    int* out_has_node
);
TF_API int tf_blueprint_get_node_count(
    tf_blueprint_t handle,
    size_t* out_count
);
TF_API int tf_blueprint_get_edge_count(
    tf_blueprint_t handle,
    size_t* out_count
);
TF_API int tf_blueprint_is_valid(
    tf_blueprint_t handle,
    int* out_is_valid
);
TF_API int tf_blueprint_validate_copy(
    tf_blueprint_t handle,
    char** out_message,
    size_t* out_size
);
TF_API int tf_blueprint_to_json_copy(
    tf_blueprint_t handle,
    char** out_json,
    size_t* out_size
);

TF_API int tf_orchestrator_create(tf_orchestrator_t* out_handle);
TF_API int tf_orchestrator_create_ex(
    const struct TF_OrchestratorOptions* options,
    tf_orchestrator_t* out_handle
);
TF_API int tf_orchestrator_destroy(tf_orchestrator_t handle);
TF_API int tf_orchestrator_register_task_callback(
    tf_orchestrator_t handle,
    const char* task_type,
    tf_task_callback_fn callback,
    void* userdata
);
TF_API int tf_orchestrator_has_task(
    tf_orchestrator_t handle,
    const char* task_type,
    int* out_has_task
);
TF_API int tf_orchestrator_register_blueprint(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    tf_blueprint_t blueprint_handle
);
TF_API int tf_orchestrator_register_blueprint_name(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    tf_blueprint_t blueprint_handle
);
TF_API int tf_orchestrator_register_blueprint_json(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    const char* json_content
);
TF_API int tf_orchestrator_register_blueprint_name_json(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    const char* json_content
);
TF_API int tf_orchestrator_has_blueprint(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    int* out_has_blueprint
);
TF_API int tf_orchestrator_has_blueprint_name(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    int* out_has_blueprint
);
TF_API int tf_orchestrator_get_blueprint_json_copy(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    char** out_json,
    size_t* out_size
);
TF_API int tf_orchestrator_get_blueprint_name_json_copy(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    char** out_json,
    size_t* out_size
);
TF_API int tf_orchestrator_set_event_callback(
    tf_orchestrator_t handle,
    tf_event_callback_fn callback,
    void* userdata
);
TF_API int tf_orchestrator_create_execution(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_create_execution_name(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_run_sync(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    const struct TF_RunOptions* opts,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_run_sync_name(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    const struct TF_RunOptions* opts,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_run_async(
    tf_orchestrator_t handle,
    uint64_t blueprint_id,
    const struct TF_RunOptions* opts,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_run_async_name(
    tf_orchestrator_t handle,
    const char* blueprint_name,
    const struct TF_RunOptions* opts,
    tf_execution_t* out_execution_handle
);
TF_API int tf_orchestrator_cleanup_completed_executions(
    tf_orchestrator_t handle,
    size_t* out_count
);
TF_API int tf_orchestrator_cleanup_old_executions(
    tf_orchestrator_t handle,
    uint64_t older_than_ms,
    size_t* out_count
);

TF_API int tf_execution_run_sync(
    tf_execution_t handle,
    const struct TF_RunOptions* opts,
    int* out_state
);
TF_API int tf_execution_wait(
    tf_execution_t handle,
    int* out_state
);
TF_API int tf_execution_poll(
    tf_execution_t handle,
    int* out_is_ready,
    int* out_state
);
TF_API int tf_execution_cancel(tf_execution_t handle);
TF_API int tf_execution_get_id(
    tf_execution_t handle,
    uint64_t* out_execution_id
);
TF_API int tf_execution_snapshot_json_copy(
    tf_execution_t handle,
    char** out_json,
    size_t* out_size
);
TF_API int tf_execution_get_overall_state(
    tf_execution_t handle,
    int* out_state
);
TF_API int tf_execution_get_node_state(
    tf_execution_t handle,
    uint64_t node_id,
    int* out_state
);
TF_API int tf_execution_get_node_retry_count(
    tf_execution_t handle,
    uint64_t node_id,
    int32_t* out_retry_count
);
TF_API int tf_execution_get_node_error_copy(
    tf_execution_t handle,
    uint64_t node_id,
    char** out_error,
    size_t* out_size
);
TF_API int tf_execution_count_by_state(
    tf_execution_t handle,
    int task_state,
    size_t* out_count
);
TF_API int tf_execution_is_complete(
    tf_execution_t handle,
    int* out_is_complete
);
TF_API int tf_execution_is_cancelled(
    tf_execution_t handle,
    int* out_is_cancelled
);
TF_API int tf_execution_get_start_time_epoch_ms(
    tf_execution_t handle,
    int64_t* out_epoch_ms
);
TF_API int tf_execution_get_end_time_epoch_ms(
    tf_execution_t handle,
    int64_t* out_epoch_ms
);
TF_API int tf_execution_context_contains(
    tf_execution_t handle,
    const char* key,
    int* out_contains
);
TF_API int tf_execution_context_set_bool(
    tf_execution_t handle,
    const char* key,
    int value
);
TF_API int tf_execution_context_get_bool(
    tf_execution_t handle,
    const char* key,
    int* out_value
);
TF_API int tf_execution_context_set_int64(
    tf_execution_t handle,
    const char* key,
    int64_t value
);
TF_API int tf_execution_context_get_int64(
    tf_execution_t handle,
    const char* key,
    int64_t* out_value
);
TF_API int tf_execution_context_set_uint64(
    tf_execution_t handle,
    const char* key,
    uint64_t value
);
TF_API int tf_execution_context_get_uint64(
    tf_execution_t handle,
    const char* key,
    uint64_t* out_value
);
TF_API int tf_execution_context_set_double(
    tf_execution_t handle,
    const char* key,
    double value
);
TF_API int tf_execution_context_get_double(
    tf_execution_t handle,
    const char* key,
    double* out_value
);
TF_API int tf_execution_context_set_string(
    tf_execution_t handle,
    const char* key,
    const char* value
);
TF_API int tf_execution_context_get_string_copy(
    tf_execution_t handle,
    const char* key,
    char** out_value,
    size_t* out_size
);
TF_API int tf_execution_result_get_bool(
    tf_execution_t handle,
    uint64_t from_node_id,
    const char* key,
    int* out_value
);
TF_API int tf_execution_result_get_int64(
    tf_execution_t handle,
    uint64_t from_node_id,
    const char* key,
    int64_t* out_value
);
TF_API int tf_execution_result_get_uint64(
    tf_execution_t handle,
    uint64_t from_node_id,
    const char* key,
    uint64_t* out_value
);
TF_API int tf_execution_result_get_double(
    tf_execution_t handle,
    uint64_t from_node_id,
    const char* key,
    double* out_value
);
TF_API int tf_execution_result_get_string_copy(
    tf_execution_t handle,
    uint64_t from_node_id,
    const char* key,
    char** out_value,
    size_t* out_size
);
TF_API int tf_execution_destroy(tf_execution_t handle);

TF_API int tf_task_context_get_exec_id(
    tf_task_context_t ctx,
    uint64_t* out_exec_id
);
TF_API int tf_task_context_get_node_id(
    tf_task_context_t ctx,
    uint64_t* out_node_id
);
TF_API int tf_task_context_report_progress(
    tf_task_context_t ctx,
    double progress
);
TF_API int tf_task_context_get_progress(
    tf_task_context_t ctx,
    double* out_progress
);
TF_API int tf_task_context_is_cancelled(
    tf_task_context_t ctx,
    int* out_is_cancelled
);
TF_API int tf_task_context_request_cancel(tf_task_context_t ctx);
TF_API int tf_task_context_set_error(
    tf_task_context_t ctx,
    const char* error_message
);
TF_API int tf_task_context_contains(
    tf_task_context_t ctx,
    const char* key,
    int* out_contains
);
TF_API int tf_task_context_set_bool(
    tf_task_context_t ctx,
    const char* key,
    int value
);
TF_API int tf_task_context_get_bool(
    tf_task_context_t ctx,
    const char* key,
    int* out_value
);
TF_API int tf_task_context_set_int64(
    tf_task_context_t ctx,
    const char* key,
    int64_t value
);
TF_API int tf_task_context_get_int64(
    tf_task_context_t ctx,
    const char* key,
    int64_t* out_value
);
TF_API int tf_task_context_set_uint64(
    tf_task_context_t ctx,
    const char* key,
    uint64_t value
);
TF_API int tf_task_context_get_uint64(
    tf_task_context_t ctx,
    const char* key,
    uint64_t* out_value
);
TF_API int tf_task_context_set_double(
    tf_task_context_t ctx,
    const char* key,
    double value
);
TF_API int tf_task_context_get_double(
    tf_task_context_t ctx,
    const char* key,
    double* out_value
);
TF_API int tf_task_context_set_string(
    tf_task_context_t ctx,
    const char* key,
    const char* value
);
TF_API int tf_task_context_get_string_copy(
    tf_task_context_t ctx,
    const char* key,
    char** out_value,
    size_t* out_size
);
TF_API int tf_task_context_set_result_bool(
    tf_task_context_t ctx,
    const char* key,
    int value
);
TF_API int tf_task_context_get_result_bool(
    tf_task_context_t ctx,
    uint64_t from_node_id,
    const char* key,
    int* out_value
);
TF_API int tf_task_context_set_result_int64(
    tf_task_context_t ctx,
    const char* key,
    int64_t value
);
TF_API int tf_task_context_get_result_int64(
    tf_task_context_t ctx,
    uint64_t from_node_id,
    const char* key,
    int64_t* out_value
);
TF_API int tf_task_context_set_result_uint64(
    tf_task_context_t ctx,
    const char* key,
    uint64_t value
);
TF_API int tf_task_context_get_result_uint64(
    tf_task_context_t ctx,
    uint64_t from_node_id,
    const char* key,
    uint64_t* out_value
);
TF_API int tf_task_context_set_result_double(
    tf_task_context_t ctx,
    const char* key,
    double value
);
TF_API int tf_task_context_get_result_double(
    tf_task_context_t ctx,
    uint64_t from_node_id,
    const char* key,
    double* out_value
);
TF_API int tf_task_context_set_result_string(
    tf_task_context_t ctx,
    const char* key,
    const char* value
);
TF_API int tf_task_context_get_result_string_copy(
    tf_task_context_t ctx,
    uint64_t from_node_id,
    const char* key,
    char** out_value,
    size_t* out_size
);

TF_API void tf_string_free(char* value);
TF_API const char* tf_error_message(int error_code);
TF_API const char* tf_task_state_name(int task_state);

#ifdef __cplusplus
}
#endif
